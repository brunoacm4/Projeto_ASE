#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"

#if CONFIG_PROJECT_ENABLE_MQTT
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#endif

#include "mqtt_app.h"

#if CONFIG_PROJECT_ENABLE_MQTT
static const char *TAG = "mqtt_app";

static esp_mqtt_client_handle_t s_client;
static bool s_started;
static bool s_connected;

#define MQTT_PUBLISH_DRAIN_MS 750

static esp_err_t mqtt_wait_connected(TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();

    while (!s_connected && (xTaskGetTickCount() - start) < timeout_ticks) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (!s_connected) {
        ESP_LOGW(TAG, "MQTT connection timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_publish(event->client, "ase/access/status", "online", 0, 0, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGI(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data: %.*s -> %.*s",
                 event->topic_len, event->topic, event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;
    default:
        break;
    }
}
#endif

esp_err_t mqtt_app_start_once(void)
{
#if !CONFIG_PROJECT_ENABLE_MQTT
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_started) {
        return mqtt_wait_connected(pdMS_TO_TICKS(3000));
    }

    ESP_LOGI(TAG, "Starting MQTT network");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = example_connect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Network connection failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Starting MQTT client: %s", CONFIG_PROJECT_BROKER_URL);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_PROJECT_BROKER_URL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ret = esp_mqtt_client_start(s_client);
    if (ret == ESP_OK) {
        s_started = true;
        ret = mqtt_wait_connected(pdMS_TO_TICKS(3000));
    }
    return ret;
#endif
}

esp_err_t mqtt_app_publish(const char *topic, const char *payload, int qos, int retain)
{
#if !CONFIG_PROJECT_ENABLE_MQTT
    (void)topic;
    (void)payload;
    (void)qos;
    (void)retain;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_started) {
        ESP_LOGW(TAG, "MQTT publish skipped: client not started");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_connected) {
        ESP_LOGW(TAG, "MQTT publish skipped: client not connected");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "MQTT publish failed: topic=%s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MQTT publish queued: topic=%s msg_id=%d payload=%s", topic, msg_id, payload);
    vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_DRAIN_MS));
    return ESP_OK;
#endif
}
