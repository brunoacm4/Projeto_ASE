#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "access_control.h"
#include "display_ui.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_app.h"
#include "multisensor_dht20.h"
#include "pir.h"
#include "project_config.h"
#include "rfid.h"
#include "storage_log.h"

static const char *TAG = "access_monitor";

RTC_DATA_ATTR static uint32_t s_boot_count;
RTC_DATA_ATTR static bool s_access_blocked;

typedef struct {
    float temperature;
    float humidity;
    bool valid;
    bool blocked;
} env_sample_t;

static const char *wake_reason_name(esp_sleep_wakeup_cause_t reason)
{
    switch (reason) {
    case ESP_SLEEP_WAKEUP_TIMER:
        return "timer";
    case ESP_SLEEP_WAKEUP_EXT1:
        return "pir";
    case ESP_SLEEP_WAKEUP_GPIO:
        return "gpio";
    default:
        return "boot";
    }
}

static env_sample_t read_environment(void)
{
    env_sample_t sample = {
        .temperature = NAN,
        .humidity = NAN,
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_dev_handle_t dht20_dev = NULL;

    esp_err_t ret = dht20_init(&i2c_bus,
                               &dht20_dev,
                               DHT20_SENSOR_ADDR,
                               PROJECT_PIN_DHT20_SDA,
                               PROJECT_PIN_DHT20_SCL,
                               DHT20_SCL_DFLT_FREQ_HZ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DHT20 init failed (%s)", esp_err_to_name(ret));
        sample.blocked = true;
        s_access_blocked = true;
        return sample;
    }

    ret = dht20_read_data(dht20_dev, &sample.humidity, &sample.temperature);
    if (ret == ESP_OK) {
        sample.valid = true;
        sample.blocked = sample.temperature > PROJECT_TEMP_MAX_C || sample.humidity > PROJECT_HUM_MAX_PCT;
        s_access_blocked = sample.blocked;
        ESP_LOGI(TAG, "DHT20 temp=%.2f hum=%.2f blocked=%d",
                 sample.temperature, sample.humidity, sample.blocked);
    } else {
        ESP_LOGE(TAG, "DHT20 read failed (%s)", esp_err_to_name(ret));
        sample.blocked = true;
        s_access_blocked = true;
    }

    dht20_free(i2c_bus, dht20_dev);
    return sample;
}

static void publish_env_event(const char *event, const env_sample_t *sample)
{
    char payload[160];
    snprintf(payload,
             sizeof(payload),
             "{\"event\":\"%s\",\"temp\":%.2f,\"hum\":%.2f,\"blocked\":%d}",
             event,
             sample->temperature,
             sample->humidity,
             sample->blocked ? 1 : 0);
    mqtt_app_publish(sample->blocked ? "ase/access/alerts" : "ase/access/env", payload, 0, 0);
}

static void publish_access_event(const char *uid, const char *result, const env_sample_t *sample)
{
    char payload[192];
    snprintf(payload,
             sizeof(payload),
             "{\"event\":\"rfid_auth\",\"uid\":\"%s\",\"result\":\"%s\",\"temp\":%.2f,\"hum\":%.2f,\"blocked\":%d}",
             uid,
             result,
             sample->temperature,
             sample->humidity,
             sample->blocked ? 1 : 0);
    mqtt_app_publish("ase/access/events", payload, 0, 0);
}

static void log_env_event(const char *wakeup, const char *event, const env_sample_t *sample, const char *result)
{
    if (storage_log_mount() == ESP_OK) {
        storage_log_append(wakeup,
                           event,
                           sample->temperature,
                           sample->humidity,
                           "",
                           result,
                           sample->blocked || s_access_blocked);
    }
}

static void enter_deep_sleep(void)
{
    storage_log_unmount();
    display_ui_power_down();

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(PROJECT_ENV_WAKE_PERIOD_US));
    ESP_ERROR_CHECK(pir_configure_wakeup());

    ESP_LOGI(TAG, "Entering deep-sleep");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_deep_sleep_start();
}

static void handle_timer_wakeup(const char *wakeup)
{
    env_sample_t sample = read_environment();
    if (!sample.valid) {
        if (display_ui_init() == ESP_OK) {
            display_ui_show_sensor_error();
            vTaskDelay(pdMS_TO_TICKS(PROJECT_ALERT_DISPLAY_MS));
            display_ui_power_down();
        }
        log_env_event(wakeup, "env_error", &sample, "sensor_error");
        return;
    }

    if (display_ui_init() == ESP_OK) {
        if (sample.blocked) {
            display_ui_show_alert(sample.temperature, sample.humidity);
            vTaskDelay(pdMS_TO_TICKS(PROJECT_ALERT_DISPLAY_MS));
        } else {
            display_ui_show_env(sample.temperature, sample.humidity, false);
            vTaskDelay(pdMS_TO_TICKS(PROJECT_ENV_DISPLAY_MS));
        }
        display_ui_power_down();
    }

    mqtt_app_start_once();
    log_env_event(wakeup, sample.blocked ? "env_alert" : "env_sample", &sample, sample.blocked ? "blocked" : "ok");
    publish_env_event(sample.blocked ? "env_alert" : "env_sample", &sample);
}

static void handle_pir_wakeup(const char *wakeup)
{
    if (display_ui_init() == ESP_OK) {
        display_ui_show_wait_rfid_pending();
    }

    env_sample_t sample = read_environment();
    sample.blocked = sample.blocked || s_access_blocked;

    storage_log_mount();

    if (!sample.valid) {
        display_ui_show_sensor_error();
        storage_log_append(wakeup, "env_error", sample.temperature, sample.humidity, "", "sensor_error", true);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
        display_ui_power_down();
        mqtt_app_start_once();
        return;
    }

    if (sample.blocked) {
        display_ui_show_alert(sample.temperature, sample.humidity);
    } else {
        display_ui_show_wait_rfid(sample.temperature, sample.humidity);
    }

    esp_err_t rfid_ret = rfid_init();
    if (rfid_ret != ESP_OK) {
        ESP_LOGE(TAG, "RFID init failed (%s)", esp_err_to_name(rfid_ret));
        display_ui_show_access_denied("NO_RFID", "RFID ERROR");
        storage_log_append(wakeup, "rfid_error", sample.temperature, sample.humidity, "", "rfid_error", true);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
        display_ui_power_down();
        return;
    }

    rfid_uid_t uid = {0};
    char uid_str[RFID_UID_STR_LEN] = "NO_CARD";
    int64_t deadline = esp_timer_get_time() + ((int64_t)PROJECT_RFID_WAIT_TIMEOUT_MS * 1000);

    while (esp_timer_get_time() < deadline) {
        if (rfid_read_uid(&uid) == ESP_OK) {
            rfid_uid_to_string(&uid, uid_str, sizeof(uid_str));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(PROJECT_RFID_POLL_MS));
    }

    bool has_card = strcmp(uid_str, "NO_CARD") != 0;
    bool authorized = has_card && access_control_is_authorized(uid.bytes, uid.len);
    bool granted = authorized && !sample.blocked;
    const char *result = granted ? "granted" : "denied";
    const char *reason = "CARTAO";

    if (!has_card) {
        reason = "TIMEOUT";
    } else if (sample.blocked) {
        reason = "ALERTA";
    }

    if (granted) {
        display_ui_show_access_granted(uid_str);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_OPEN_MS));
    } else {
        display_ui_show_access_denied(uid_str, reason);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
    }
    display_ui_power_down();

    storage_log_append(wakeup,
                       "rfid_auth",
                       sample.temperature,
                       sample.humidity,
                       has_card ? uid_str : "",
                       result,
                       sample.blocked);
    mqtt_app_start_once();
    publish_access_event(has_card ? uid_str : "", result, &sample);
}

void app_main(void)
{
    s_boot_count++;
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    const char *wakeup = wake_reason_name(reason);

    ESP_LOGI(TAG, "Boot %lu, wakeup=%s, blocked=%d",
             (unsigned long)s_boot_count, wakeup, s_access_blocked);

    if (reason == ESP_SLEEP_WAKEUP_EXT1) {
        handle_pir_wakeup(wakeup);
    } else {
        handle_timer_wakeup(wakeup);
    }

    enter_deep_sleep();
}
