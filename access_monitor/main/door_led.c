#include <stdbool.h>
#include <stdint.h>

#include "door_led.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_config.h"

static const char *TAG = "door_led";

#define RMT_LED_RESOLUTION_HZ 10000000
#define DOOR_LED_BRIGHTNESS   150

static rmt_channel_handle_t s_led_channel;
static rmt_encoder_handle_t s_led_encoder;
static bool s_ready;

static const rmt_symbol_word_t ws2812_zero = {
    .level0 = 1,
    .duration0 = 0.3 * RMT_LED_RESOLUTION_HZ / 1000000,
    .level1 = 0,
    .duration1 = 0.9 * RMT_LED_RESOLUTION_HZ / 1000000,
};

static const rmt_symbol_word_t ws2812_one = {
    .level0 = 1,
    .duration0 = 0.9 * RMT_LED_RESOLUTION_HZ / 1000000,
    .level1 = 0,
    .duration1 = 0.3 * RMT_LED_RESOLUTION_HZ / 1000000,
};

static const rmt_symbol_word_t ws2812_reset = {
    .level0 = 0,
    .duration0 = RMT_LED_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = RMT_LED_RESOLUTION_HZ / 1000000 * 50 / 2,
};

static size_t ws2812_encoder_callback(const void *data,
                                      size_t data_size,
                                      size_t symbols_written,
                                      size_t symbols_free,
                                      rmt_symbol_word_t *symbols,
                                      bool *done,
                                      void *arg)
{
    (void)arg;
    if (symbols_free < 8) {
        return 0;
    }

    size_t data_pos = symbols_written / 8;
    const uint8_t *bytes = data;
    if (data_pos < data_size) {
        size_t symbol_pos = 0;
        for (int bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            symbols[symbol_pos++] = (bytes[data_pos] & bitmask) ? ws2812_one : ws2812_zero;
        }
        return symbol_pos;
    }

    symbols[0] = ws2812_reset;
    *done = true;
    return 1;
}

esp_err_t door_led_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = PROJECT_PIN_RGB_LED,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_RESOLUTION_HZ,
        .trans_queue_depth = 1,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_config, &s_led_channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RMT channel init failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    rmt_simple_encoder_config_t encoder_config = {
        .callback = ws2812_encoder_callback,
    };
    ret = rmt_new_simple_encoder(&encoder_config, &s_led_encoder);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RMT encoder init failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_enable(s_led_channel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "RMT enable failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_ready = true;
    uint8_t grb[3] = {0, 0, 0};
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };
    if (rmt_transmit(s_led_channel, s_led_encoder, grb, sizeof(grb), &transmit_config) == ESP_OK) {
        rmt_tx_wait_all_done(s_led_channel, pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

static void door_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (door_led_init() != ESP_OK) {
        return;
    }

    uint8_t grb[3] = {green, red, blue};
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    if (rmt_transmit(s_led_channel, s_led_encoder, grb, sizeof(grb), &tx_config) == ESP_OK) {
        rmt_tx_wait_all_done(s_led_channel, pdMS_TO_TICKS(100));
    }
}

void door_led_open(void)
{
    door_led_set_rgb(0, DOOR_LED_BRIGHTNESS, 0);
}

void door_led_denied(void)
{
    for (int i = 0; i < 3; i++) {
        door_led_set_rgb(0, DOOR_LED_BRIGHTNESS, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
        door_led_off();
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void door_led_off(void)
{
    if (!s_ready) {
        return;
    }
    door_led_set_rgb(0, 0, 0);
}
