#include <stdbool.h>
#include <stdio.h>

#include "display_ui.h"
#include "driver/gpio.h"
#include "project_config.h"
#include "spi_bus_manager.h"
#include "st7735.h"

static bool s_ready;

esp_err_t display_ui_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t ret = project_spi_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }

    st7735_config_t cfg = {
        .mosi_io_num = PROJECT_PIN_SPI_MOSI,
        .miso_io_num = PROJECT_PIN_SPI_MISO,
        .sclk_io_num = PROJECT_PIN_SPI_SCLK,
        .cs_io_num = PROJECT_PIN_TFT_CS,
        .dc_io_num = PROJECT_PIN_TFT_DC,
        .rst_io_num = PROJECT_PIN_TFT_RST,
        .bl_io_num = PROJECT_PIN_TFT_BL,
        .host_id = PROJECT_SPI_HOST,
        .skip_bus_init = true,
    };

    ret = st7735_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    st7735_set_rotation(1);
    st7735_fill_screen(ST7735_BLACK);
    s_ready = true;
    return ESP_OK;
}

void display_ui_show_boot(const char *reason)
{
    if (!s_ready) {
        return;
    }

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 6, "ACCESS MONITOR", ST7735_CYAN, ST7735_BLACK, 1);
    st7735_draw_string(5, 24, "BOOT", ST7735_WHITE, ST7735_BLACK, 1);
    st7735_draw_string(45, 24, reason, ST7735_YELLOW, ST7735_BLACK, 1);
}

void display_ui_show_env(float temperature, float humidity, bool blocked, size_t occupancy)
{
    if (!s_ready) {
        return;
    }

    char temp[20];
    char hum[20];
    char people[24];
    snprintf(temp, sizeof(temp), "%.1f C", temperature);
    snprintf(hum, sizeof(hum), "%.1f %%", humidity);
    snprintf(people, sizeof(people), "DENTRO %u", (unsigned)occupancy);

    uint16_t state_color = blocked ? ST7735_RED : ST7735_GREEN;
    const char *state = blocked ? "BLOCKED" : "NORMAL";

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 5, "ENV SAMPLE", ST7735_CYAN, ST7735_BLACK, 1);
    st7735_draw_string(5, 25, "TEMP", ST7735_GRAY, ST7735_BLACK, 1);
    st7735_draw_string(70, 25, temp, ST7735_YELLOW, ST7735_BLACK, 1);
    st7735_draw_string(5, 40, "HUM", ST7735_GRAY, ST7735_BLACK, 1);
    st7735_draw_string(70, 40, hum, ST7735_YELLOW, ST7735_BLACK, 1);
    st7735_draw_string(5, 55, people, ST7735_WHITE, ST7735_BLACK, 1);
    st7735_draw_string(95, 55, state, state_color, ST7735_BLACK, 1);
}

void display_ui_show_presence(size_t occupancy, const char *const *card_ids, size_t card_count)
{
    if (!s_ready) {
        return;
    }

    char title[24];
    snprintf(title, sizeof(title), "DENTRO %u", (unsigned)occupancy);

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 5, title, ST7735_CYAN, ST7735_BLACK, 1);

    if (occupancy == 0) {
        st7735_draw_string(5, 28, "SEM CARTOES", ST7735_WHITE, ST7735_BLACK, 1);
        return;
    }

    size_t shown = card_count < 3 ? card_count : 3;
    for (size_t i = 0; i < shown; i++) {
        st7735_draw_string(5, 24 + (uint16_t)(i * 16), card_ids[i], ST7735_WHITE, ST7735_BLACK, 1);
    }
}

void display_ui_show_wait_rfid_pending(void)
{
    if (!s_ready) {
        return;
    }

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 5, "MOVIMENTO", ST7735_YELLOW, ST7735_BLACK, 1);
    st7735_draw_string(5, 22, "APROXIME CARTAO", ST7735_WHITE, ST7735_BLACK, 1);
    st7735_draw_string(5, 50, "A VERIFICAR...", ST7735_CYAN, ST7735_BLACK, 1);
}

void display_ui_show_wait_rfid(float temperature, float humidity)
{
    if (!s_ready) {
        return;
    }

    char line[24];
    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 5, "MOVIMENTO", ST7735_YELLOW, ST7735_BLACK, 1);
    st7735_draw_string(5, 22, "APROXIME CARTAO", ST7735_WHITE, ST7735_BLACK, 1);
    snprintf(line, sizeof(line), "T %.1fC H %.1f%%", temperature, humidity);
    st7735_draw_string(5, 50, line, ST7735_CYAN, ST7735_BLACK, 1);
}

void display_ui_show_access_granted(const char *uid)
{
    if (!s_ready) {
        return;
    }

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 8, "ACESSO", ST7735_GREEN, ST7735_BLACK, 2);
    st7735_draw_string(5, 32, "AUTORIZADO", ST7735_GREEN, ST7735_BLACK, 1);
    st7735_draw_string(5, 55, uid, ST7735_WHITE, ST7735_BLACK, 1);
}

void display_ui_show_access_denied(const char *uid, const char *reason)
{
    if (!s_ready) {
        return;
    }

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 8, "ACESSO", ST7735_RED, ST7735_BLACK, 2);
    st7735_draw_string(5, 32, "NEGADO", ST7735_RED, ST7735_BLACK, 1);
    st7735_draw_string(5, 48, reason, ST7735_YELLOW, ST7735_BLACK, 1);
    st7735_draw_string(5, 64, uid, ST7735_WHITE, ST7735_BLACK, 1);
}

void display_ui_show_timeout(void)
{
    if (!s_ready) {
        return;
    }

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(12, 25, "TIMEOUT", ST7735_YELLOW, ST7735_BLACK, 2);
}

void display_ui_show_alert(float temperature, float humidity)
{
    if (!s_ready) {
        return;
    }

    char line[24];
    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 8, "ALERTA", ST7735_RED, ST7735_BLACK, 2);
    st7735_draw_string(5, 34, "ACESSO BLOQUEADO", ST7735_RED, ST7735_BLACK, 1);
    snprintf(line, sizeof(line), "%.1fC %.1f%%", temperature, humidity);
    st7735_draw_string(5, 56, line, ST7735_YELLOW, ST7735_BLACK, 1);
}

void display_ui_show_sensor_error(void)
{
    if (!s_ready) {
        return;
    }

    st7735_fill_screen(ST7735_BLACK);
    st7735_draw_string(5, 8, "ERRO", ST7735_RED, ST7735_BLACK, 2);
    st7735_draw_string(5, 34, "SENSOR DHT20", ST7735_RED, ST7735_BLACK, 1);
    st7735_draw_string(5, 56, "ACESSO BLOQUEADO", ST7735_YELLOW, ST7735_BLACK, 1);
}

void display_ui_power_down(void)
{
    if (s_ready) {
        st7735_fill_screen(ST7735_BLACK);
    }
    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << PROJECT_PIN_TFT_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_cfg);
    gpio_set_level(PROJECT_PIN_TFT_BL, 0);
}
