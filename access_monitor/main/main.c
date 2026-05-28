#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "access_control.h"
#include "display_ui.h"
#include "door_led.h"
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
#include "presence.h"
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

static int64_t now_millis(void)
{
    return esp_timer_get_time() / 1000;
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
    char payload[224];
    snprintf(payload,
             sizeof(payload),
             "{\"event\":\"%s\",\"card_id\":\"\",\"direction\":\"\",\"occupancy\":%u,\"temp\":%.2f,\"hum\":%.2f,\"blocked\":%d,\"millis\":%lld}",
             event,
             (unsigned)presence_count(),
             sample->temperature,
             sample->humidity,
             sample->blocked ? 1 : 0,
             now_millis());
    mqtt_app_publish(sample->blocked ? "ase/access/alerts" : "ase/access/env", payload, 0, 0);
}

static void publish_access_event(const char *event,
                                 const char *uid,
                                 const char *direction,
                                 const char *result,
                                 const env_sample_t *sample)
{
    char payload[256];
    snprintf(payload,
             sizeof(payload),
             "{\"event\":\"%s\",\"card_id\":\"%s\",\"direction\":\"%s\",\"result\":\"%s\",\"occupancy\":%u,\"temp\":%.2f,\"hum\":%.2f,\"blocked\":%d,\"millis\":%lld}",
             event,
             uid,
             direction ? direction : "",
             result,
             (unsigned)presence_count(),
             sample->temperature,
             sample->humidity,
             sample->blocked ? 1 : 0,
             now_millis());
    mqtt_app_publish("ase/access/events", payload, 0, 0);
}

static void publish_presence_event(void)
{
    char payload[96];
    snprintf(payload,
             sizeof(payload),
             "{\"event\":\"presence\",\"occupancy\":%u,\"millis\":%lld}",
             (unsigned)presence_count(),
             now_millis());
    mqtt_app_publish("ase/access/presence", payload, 0, 0);
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

static void log_access_event(const char *event, const char *card_id, const char *result, const env_sample_t *sample)
{
    if (storage_log_mount() == ESP_OK) {
        storage_log_append_access(event,
                                  card_id,
                                  result,
                                  sample->temperature,
                                  sample->humidity,
                                  sample->blocked || s_access_blocked);
        storage_log_write_presence_snapshot();
    }
}

static void display_env_with_presence(const env_sample_t *sample)
{
    size_t count = presence_count();
    display_ui_show_env(sample->temperature, sample->humidity, sample->blocked, count);
    vTaskDelay(pdMS_TO_TICKS(PROJECT_ENV_DISPLAY_MS));

    if (count > 0) {
        presence_entry_t entries[PROJECT_MAX_PRESENT_CARDS];
        size_t total = presence_snapshot(entries, PROJECT_MAX_PRESENT_CARDS);
        const char *ids[PROJECT_MAX_PRESENT_CARDS];
        size_t shown = total < PROJECT_MAX_PRESENT_CARDS ? total : PROJECT_MAX_PRESENT_CARDS;
        for (size_t i = 0; i < shown; i++) {
            ids[i] = entries[i].card_id;
        }
        display_ui_show_presence(total, ids, shown);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ENV_DISPLAY_MS));
    }
}

static void evacuate_present_cards(const env_sample_t *sample)
{
    presence_entry_t entries[PROJECT_MAX_PRESENT_CARDS];
    size_t count = presence_snapshot(entries, PROJECT_MAX_PRESENT_CARDS);
    if (count == 0) {
        if (storage_log_mount() == ESP_OK) {
            storage_log_write_presence_snapshot();
        }
        publish_presence_event();
        return;
    }

    for (size_t i = 0; i < count && i < PROJECT_MAX_PRESENT_CARDS; i++) {
        log_access_event("evacuated", entries[i].card_id, "evacuated", sample);
        publish_access_event("evacuated", entries[i].card_id, "evacuated", "evacuated", sample);
    }
    presence_clear();
    if (storage_log_mount() == ESP_OK) {
        storage_log_write_presence_snapshot();
    }
    publish_presence_event();
}

static void enter_deep_sleep(void)
{
    door_led_off();
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
    storage_log_mount();
    if (!sample.valid) {
        if (display_ui_init() == ESP_OK) {
            display_ui_show_sensor_error();
            vTaskDelay(pdMS_TO_TICKS(PROJECT_ALERT_DISPLAY_MS));
            display_ui_power_down();
        }
        log_env_event(wakeup, "env_error", &sample, "sensor_error");
        mqtt_app_start_once();
        publish_env_event("env_error", &sample);
        return;
    }

    if (display_ui_init() == ESP_OK) {
        if (sample.blocked) {
            display_ui_show_alert(sample.temperature, sample.humidity);
            vTaskDelay(pdMS_TO_TICKS(PROJECT_ALERT_DISPLAY_MS));
        } else {
            display_env_with_presence(&sample);
        }
        display_ui_power_down();
    }

    mqtt_app_start_once();
    log_env_event(wakeup, sample.blocked ? "env_alert" : "env_sample", &sample, sample.blocked ? "blocked" : "ok");
    publish_env_event(sample.blocked ? "env_alert" : "env_sample", &sample);
    if (sample.blocked) {
        evacuate_present_cards(&sample);
    }
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
        log_access_event("env_error", "", "sensor_error", &sample);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
        display_ui_power_down();
        mqtt_app_start_once();
        publish_env_event("env_error", &sample);
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
        log_access_event("rfid_error", "", "rfid_error", &sample);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
        display_ui_power_down();
        mqtt_app_start_once();
        publish_access_event("rfid_error", "", "", "rfid_error", &sample);
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
    bool can_open = authorized && !sample.blocked;
    const char *event = "denied";
    const char *result = can_open ? "granted" : "denied";
    const char *direction = "";
    const char *reason = "CARTAO";

    if (!has_card) {
        event = "timeout";
        result = "timeout";
        reason = "TIMEOUT";
    } else if (sample.blocked) {
        reason = "ALERTA";
    } else if (authorized) {
        presence_action_t action = presence_toggle(uid_str, now_millis());
        if (action == PRESENCE_ACTION_ENTERED) {
            event = "entry";
            direction = "entry";
        } else if (action == PRESENCE_ACTION_EXITED) {
            event = "exit";
            direction = "exit";
        } else {
            event = "denied";
            result = "full";
            reason = "LOTADO";
            can_open = false;
        }
    }

    if (!has_card) {
        display_ui_show_timeout();
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
    } else if (can_open) {
        door_led_open();
        display_ui_show_access_granted(uid_str);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_OPEN_MS));
    } else {
        door_led_denied();
        display_ui_show_access_denied(uid_str, reason);
        vTaskDelay(pdMS_TO_TICKS(PROJECT_ACCESS_DENIED_MS));
    }
    door_led_off();
    display_ui_power_down();

    storage_log_append(wakeup,
                       event,
                       sample.temperature,
                       sample.humidity,
                       has_card ? uid_str : "",
                       result,
                       sample.blocked);
    log_access_event(event, has_card ? uid_str : "", result, &sample);
    mqtt_app_start_once();
    publish_access_event(event, has_card ? uid_str : "", direction, result, &sample);
    publish_presence_event();
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
