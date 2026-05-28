#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "pir.h"
#include "project_config.h"

esp_err_t pir_configure_wakeup(void)
{
    rtc_gpio_init(PROJECT_PIN_PIR_OUT);
    rtc_gpio_set_direction(PROJECT_PIN_PIR_OUT, RTC_GPIO_MODE_INPUT_ONLY);

#if CONFIG_PROJECT_PIR_ACTIVE_HIGH
    rtc_gpio_pulldown_en(PROJECT_PIN_PIR_OUT);
    rtc_gpio_pullup_dis(PROJECT_PIN_PIR_OUT);
    return esp_sleep_enable_ext1_wakeup_io(1ULL << PROJECT_PIN_PIR_OUT, ESP_EXT1_WAKEUP_ANY_HIGH);
#else
    rtc_gpio_pullup_en(PROJECT_PIN_PIR_OUT);
    rtc_gpio_pulldown_dis(PROJECT_PIN_PIR_OUT);
    return esp_sleep_enable_ext1_wakeup_io(1ULL << PROJECT_PIN_PIR_OUT, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
}

