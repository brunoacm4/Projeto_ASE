#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"
#include "rfid.h"
#include "spi_bus_manager.h"

static const char *TAG = "rfid";

#define MFRC522_REG_COMMAND       0x01
#define MFRC522_REG_COM_IEN       0x02
#define MFRC522_REG_COM_IRQ       0x04
#define MFRC522_REG_ERROR         0x06
#define MFRC522_REG_FIFO_DATA     0x09
#define MFRC522_REG_FIFO_LEVEL    0x0A
#define MFRC522_REG_CONTROL       0x0C
#define MFRC522_REG_BIT_FRAMING   0x0D
#define MFRC522_REG_MODE          0x11
#define MFRC522_REG_TX_MODE       0x12
#define MFRC522_REG_RX_MODE       0x13
#define MFRC522_REG_TX_CONTROL    0x14
#define MFRC522_REG_TX_ASK        0x15
#define MFRC522_REG_T_MODE        0x2A
#define MFRC522_REG_T_PRESCALER   0x2B
#define MFRC522_REG_T_RELOAD_H    0x2C
#define MFRC522_REG_T_RELOAD_L    0x2D
#define MFRC522_REG_VERSION       0x37

#define MFRC522_CMD_IDLE          0x00
#define MFRC522_CMD_TRANSCEIVE    0x0C
#define MFRC522_CMD_SOFT_RESET    0x0F

#define PICC_CMD_REQA             0x26
#define PICC_CMD_SEL_CL1          0x93

static spi_device_handle_t s_spi;
static bool s_ready;

static esp_err_t mfrc522_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {
        (uint8_t)((reg << 1) & 0x7E),
        value,
    };
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t mfrc522_read_reg(uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = {
        (uint8_t)(((reg << 1) & 0x7E) | 0x80),
        0x00,
    };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t ret = spi_device_polling_transmit(s_spi, &t);
    if (ret == ESP_OK) {
        *value = rx[1];
    }
    return ret;
}

static esp_err_t mfrc522_set_bits(uint8_t reg, uint8_t mask)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(mfrc522_read_reg(reg, &value), TAG, "read reg failed");
    return mfrc522_write_reg(reg, value | mask);
}

static esp_err_t mfrc522_clear_bits(uint8_t reg, uint8_t mask)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(mfrc522_read_reg(reg, &value), TAG, "read reg failed");
    return mfrc522_write_reg(reg, value & (uint8_t)(~mask));
}

static esp_err_t mfrc522_antenna_on(void)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(mfrc522_read_reg(MFRC522_REG_TX_CONTROL, &value), TAG, "read tx control failed");
    if ((value & 0x03) != 0x03) {
        return mfrc522_set_bits(MFRC522_REG_TX_CONTROL, 0x03);
    }
    return ESP_OK;
}

static esp_err_t mfrc522_transceive(const uint8_t *send_data,
                                    size_t send_len,
                                    uint8_t *back_data,
                                    size_t *back_len,
                                    uint8_t valid_bits)
{
    if (send_data == NULL || send_len == 0 || back_data == NULL || back_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_IDLE), TAG, "idle failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_COM_IRQ, 0x7F), TAG, "irq clear failed");
    ESP_RETURN_ON_ERROR(mfrc522_set_bits(MFRC522_REG_FIFO_LEVEL, 0x80), TAG, "fifo flush failed");

    for (size_t i = 0; i < send_len; i++) {
        ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_FIFO_DATA, send_data[i]), TAG, "fifo write failed");
    }

    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_BIT_FRAMING, valid_bits & 0x07), TAG, "bit framing failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_TRANSCEIVE), TAG, "transceive failed");
    ESP_RETURN_ON_ERROR(mfrc522_set_bits(MFRC522_REG_BIT_FRAMING, 0x80), TAG, "start send failed");

    uint8_t irq = 0;
    for (int i = 0; i < 40; i++) {
        ESP_RETURN_ON_ERROR(mfrc522_read_reg(MFRC522_REG_COM_IRQ, &irq), TAG, "irq read failed");
        if (irq & 0x30) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_RETURN_ON_ERROR(mfrc522_clear_bits(MFRC522_REG_BIT_FRAMING, 0x80), TAG, "stop send failed");
    if ((irq & 0x30) == 0) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t error = 0;
    ESP_RETURN_ON_ERROR(mfrc522_read_reg(MFRC522_REG_ERROR, &error), TAG, "error read failed");
    if (error & 0x1B) {
        return ESP_FAIL;
    }

    uint8_t fifo_level = 0;
    ESP_RETURN_ON_ERROR(mfrc522_read_reg(MFRC522_REG_FIFO_LEVEL, &fifo_level), TAG, "fifo level failed");
    if (fifo_level > *back_len) {
        fifo_level = *back_len;
    }

    for (uint8_t i = 0; i < fifo_level; i++) {
        ESP_RETURN_ON_ERROR(mfrc522_read_reg(MFRC522_REG_FIFO_DATA, &back_data[i]), TAG, "fifo read failed");
    }

    *back_len = fifo_level;
    return ESP_OK;
}

esp_err_t rfid_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t ret = project_spi_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }

    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << PROJECT_PIN_RFID_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(PROJECT_PIN_RFID_RST, 1);

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PROJECT_PIN_RFID_CS,
        .queue_size = 1,
    };

    ret = spi_bus_add_device(PROJECT_SPI_HOST, &dev_cfg, &s_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RFID SPI add failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_COMMAND, MFRC522_CMD_SOFT_RESET), TAG, "soft reset failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_T_MODE, 0x8D), TAG, "timer mode failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_T_PRESCALER, 0x3E), TAG, "timer prescaler failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_T_RELOAD_L, 30), TAG, "timer reload low failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_T_RELOAD_H, 0), TAG, "timer reload high failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_TX_ASK, 0x40), TAG, "tx ask failed");
    ESP_RETURN_ON_ERROR(mfrc522_write_reg(MFRC522_REG_MODE, 0x3D), TAG, "mode failed");
    ESP_RETURN_ON_ERROR(mfrc522_antenna_on(), TAG, "antenna failed");

    uint8_t version = 0;
    if (mfrc522_read_reg(MFRC522_REG_VERSION, &version) == ESP_OK) {
        ESP_LOGI(TAG, "MFRC522 version 0x%02X", version);
    }

    s_ready = true;
    return ESP_OK;
}

esp_err_t rfid_read_uid(rfid_uid_t *uid)
{
    if (!s_ready || uid == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t atqa[2] = {0};
    size_t atqa_len = sizeof(atqa);
    uint8_t reqa = PICC_CMD_REQA;

    esp_err_t ret = mfrc522_transceive(&reqa, 1, atqa, &atqa_len, 7);
    if (ret != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t anticoll[] = {PICC_CMD_SEL_CL1, 0x20};
    uint8_t response[5] = {0};
    size_t response_len = sizeof(response);
    ret = mfrc522_transceive(anticoll, sizeof(anticoll), response, &response_len, 0);
    if (ret != ESP_OK || response_len < 5) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t bcc = response[0] ^ response[1] ^ response[2] ^ response[3];
    if (bcc != response[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    memcpy(uid->bytes, response, 4);
    uid->len = 4;
    return ESP_OK;
}

void rfid_uid_to_string(const rfid_uid_t *uid, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (uid == NULL) {
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; i < uid->len && pos + 2 < out_size; i++) {
        pos += snprintf(out + pos, out_size - pos, "%02X", uid->bytes[i]);
    }
}
