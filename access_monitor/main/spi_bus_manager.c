#include <stdbool.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "project_config.h"
#include "spi_bus_manager.h"

static const char *TAG = "spi_bus";
static bool s_spi_bus_ready;

esp_err_t project_spi_bus_init(void)
{
    if (s_spi_bus_ready) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PROJECT_PIN_SPI_MOSI,
        .miso_io_num = PROJECT_PIN_SPI_MISO,
        .sclk_io_num = PROJECT_PIN_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 160 * 80 * 2 + 8,
    };

    esp_err_t ret = spi_bus_initialize(PROJECT_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_ERR_INVALID_STATE) {
        s_spi_bus_ready = true;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_spi_bus_ready = true;
    return ESP_OK;
}
