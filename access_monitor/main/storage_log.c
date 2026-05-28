#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#include "driver/spi_common.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "project_config.h"
#include "sdmmc_cmd.h"
#include "spi_bus_manager.h"
#include "storage_log.h"

static const char *TAG = "storage";

static sdmmc_card_t *s_card;
static bool s_mounted;

static esp_err_t ensure_log_header(void)
{
    struct stat st;
    bool has_content = (stat(PROJECT_LOG_FILE_PATH, &st) == 0) && (st.st_size > 0);
    if (has_content) {
        return ESP_OK;
    }

    FILE *f = fopen(PROJECT_LOG_FILE_PATH, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create %s", PROJECT_LOG_FILE_PATH);
        return ESP_FAIL;
    }

    fprintf(f, "millis,wakeup,event,temp_c,humidity_pct,card_uid,result,access_blocked\n");
    fclose(f);
    return ESP_OK;
}

esp_err_t storage_log_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = PROJECT_SPI_HOST;

    esp_err_t ret = project_spi_bus_init();
    if (ret != ESP_OK) {
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PROJECT_PIN_SD_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting %s", PROJECT_MOUNT_POINT);
    ret = esp_vfs_fat_sdspi_mount(PROJECT_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card (%s)", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "Mounted SD card: %s", s_card->cid.name);
    return ensure_log_header();
}

void storage_log_unmount(void)
{
    if (!s_mounted) {
        return;
    }

    esp_vfs_fat_sdcard_unmount(PROJECT_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
}

esp_err_t storage_log_append(const char *wakeup,
                             const char *event,
                             float temperature,
                             float humidity,
                             const char *card_uid,
                             const char *result,
                             bool access_blocked)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(PROJECT_LOG_FILE_PATH, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", PROJECT_LOG_FILE_PATH);
        return ESP_FAIL;
    }

    int64_t millis = esp_timer_get_time() / 1000;
    fprintf(f,
            "%lld,%s,%s,%.2f,%.2f,%s,%s,%d\n",
            millis,
            wakeup,
            event,
            temperature,
            humidity,
            card_uid ? card_uid : "",
            result,
            access_blocked ? 1 : 0);
    fclose(f);
    return ESP_OK;
}
