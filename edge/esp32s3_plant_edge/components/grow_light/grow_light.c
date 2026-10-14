/*
 * ESP32-S3 GPIO15 补光灯 PWM 驱动
 *
 * GPIO15 只连接 MOS 驱动输入，不直接连接灯具。初始化后输出为 0%。
 */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#include "grow_light.h"

static const char *TAG = "GROW_LIGHT";

/* -------------------------- 可调驱动参数 -------------------------- */
#define GROW_LIGHT_GPIO           15
#define GROW_LIGHT_PWM_FREQUENCY  20000
#define GROW_LIGHT_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define GROW_LIGHT_PWM_RES_BITS   10
#define GROW_LIGHT_OUTPUT_INVERT  0
#define GROW_LIGHT_TEST_DUTY_PERCENT 100
#define GROW_LIGHT_TEST_DURATION_MS 5000
#define GROW_LIGHT_PWM_MODE       LEDC_LOW_SPEED_MODE
#define GROW_LIGHT_PWM_TIMER      LEDC_TIMER_3
#define GROW_LIGHT_PWM_CHANNEL    LEDC_CHANNEL_3

static bool s_initialized = false;

static uint32_t duty_percent_to_count(uint8_t duty_percent)
{
    const uint32_t duty_period = 1U << GROW_LIGHT_PWM_RES_BITS;
    uint32_t duty = (duty_period * duty_percent) / 100U;
    if (duty >= duty_period) {
        duty = duty_period - 1U;
    }
    return duty;
}

esp_err_t grow_light_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = GROW_LIGHT_PWM_MODE,
        .duty_resolution = GROW_LIGHT_PWM_RESOLUTION,
        .timer_num = GROW_LIGHT_PWM_TIMER,
        .freq_hz = GROW_LIGHT_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                        "configure grow light PWM timer failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num = GROW_LIGHT_GPIO,
        .speed_mode = GROW_LIGHT_PWM_MODE,
        .channel = GROW_LIGHT_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = GROW_LIGHT_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = GROW_LIGHT_OUTPUT_INVERT,
        },
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                        "configure grow light PWM channel failed");

    s_initialized = true;
    ESP_LOGI(TAG, "GPIO%d PWM ready: %d Hz, duty=0%%",
             GROW_LIGHT_GPIO, GROW_LIGHT_PWM_FREQUENCY);
    return ESP_OK;
}

esp_err_t grow_light_set_duty_percent(uint8_t duty_percent)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call grow_light_init() first");
    ESP_RETURN_ON_FALSE(duty_percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "duty_percent must be between 0 and 100");

    ESP_RETURN_ON_ERROR(ledc_set_duty(GROW_LIGHT_PWM_MODE,
                                      GROW_LIGHT_PWM_CHANNEL,
                                      duty_percent_to_count(duty_percent)),
                        TAG, "set grow light PWM duty failed");
    return ledc_update_duty(GROW_LIGHT_PWM_MODE,
                            GROW_LIGHT_PWM_CHANNEL);
}

esp_err_t grow_light_stop(void)
{
    return grow_light_set_duty_percent(0);
}

esp_err_t grow_light_run_test(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call grow_light_init() first");

    ESP_LOGI(TAG, "Grow light test START: duty=%d%%, duration=%d ms",
             GROW_LIGHT_TEST_DUTY_PERCENT, GROW_LIGHT_TEST_DURATION_MS);
    ESP_RETURN_ON_ERROR(grow_light_set_duty_percent(
                            GROW_LIGHT_TEST_DUTY_PERCENT),
                        TAG, "turn on grow light test failed");

    vTaskDelay(pdMS_TO_TICKS(GROW_LIGHT_TEST_DURATION_MS));

    ESP_RETURN_ON_ERROR(grow_light_stop(), TAG,
                        "turn off grow light test failed");
    ESP_LOGI(TAG, "Grow light test DONE: GPIO%d duty=0%%", GROW_LIGHT_GPIO);
    return ESP_OK;
}
