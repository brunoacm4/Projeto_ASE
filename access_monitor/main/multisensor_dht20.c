#include "multisensor_dht20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "dht20";

// Inicialização (idêntica à do TC74, mas adaptada ao DHT20) [cite: 366]
esp_err_t dht20_init(i2c_master_bus_handle_t* pBusHandle, i2c_master_dev_handle_t* pSensorHandle,
                     uint8_t sensorAddr, int sdaPin, int sclPin, uint32_t clkSpeedHz) {
    if (pBusHandle == NULL || pSensorHandle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_config_t i2cMasterCfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = sclPin,
        .sda_io_num = sdaPin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2cMasterCfg, pBusHandle), TAG, "I2C bus init failed");

    i2c_device_config_t i2cDevCfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = sensorAddr,
        .scl_speed_hz = clkSpeedHz,
    };
    esp_err_t ret = i2c_master_bus_add_device(*pBusHandle, &i2cDevCfg, pSensorHandle);
    if (ret != ESP_OK) {
        i2c_del_master_bus(*pBusHandle);
        *pBusHandle = NULL;
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t dht20_read_data(i2c_master_dev_handle_t sensorHandle, float* pHumidity, float* pTemperature) {
    if (sensorHandle == NULL || pHumidity == NULL || pTemperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t trigger_cmd[] = {0xAC, 0x33, 0x00};
    uint8_t data[7] = {0};

    // 1. Enviar comando de medição [cite: 368]
    esp_err_t ret = i2c_master_transmit(sensorHandle, trigger_cmd, sizeof(trigger_cmd), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    // 2. Esperar pela conversão (mínimo 80ms conforme datasheet) 
    vTaskDelay(pdMS_TO_TICKS(100));

    // 3. Ler 7 bytes (Status + 20 bits Humidade + 20 bits Temp + CRC) [cite: 368]
    ret = i2c_master_receive(sensorHandle, data, sizeof(data), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;
    if ((data[0] & 0x80) != 0) {
        return ESP_ERR_TIMEOUT;
    }

    // 4. Conversão dos dados (Data acquisition & conversion) [cite: 368]
    uint32_t raw_hum = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temp = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    // Fórmulas do datasheet do DHT20 [cite: 368]
    *pHumidity = (float)raw_hum / 1048576.0f * 100.0f;
    *pTemperature = (float)raw_temp / 1048576.0f * 200.0f - 50.0f;

    if (*pHumidity < 0.0f || *pHumidity > 100.0f || *pTemperature < -20.0f || *pTemperature > 80.0f) {
        ESP_LOGW(TAG, "Ignoring implausible reading: temp=%.2f hum=%.2f", *pTemperature, *pHumidity);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

void dht20_free(i2c_master_bus_handle_t busHandle, i2c_master_dev_handle_t sensorHandle) {
    if (sensorHandle != NULL) {
        i2c_master_bus_rm_device(sensorHandle);
    }
    if (busHandle != NULL) {
        i2c_del_master_bus(busHandle);
    }
}
