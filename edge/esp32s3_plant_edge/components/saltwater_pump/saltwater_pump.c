/*
 * ESP32-S3 GPIO11 盐水泵 PWM 驱动
 *
 * GPIO11 只连接 MOS 驱动输入，不直接连接水泵。初始化后输出为 0%。
 */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#include "saltwater_pump.h"

static const char *TAG = "SALTWATER_PUMP";

/* -------------------------- 可调驱动参数 -------------------------- */
#define SALTWATER_PUMP_GPIO           11
#define SALTWATER_PUMP_PWM_FREQUENCY  20000
#define SALTWATER_PUMP_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define SALTWATER_PUMP_PWM_RES_BITS   10
#define SALTWATER_PUMP_OUTPUT_INVERT  0
#define SALTWATER_PUMP_TEST_DUTY_PERCENT 50
#define SALTWATER_PUMP_TEST_DURATION_MS 5000
#define SALTWATER_PUMP_PWM_MODE       LEDC_LOW_SPEED_MODE
#define SALTWATER_PUMP_PWM_TIMER      LEDC_TIMER_1
#define SALTWATER_PUMP_PWM_CHANNEL    LEDC_CHANNEL_1

static bool s_initialized = false;

static uint32_t duty_percent_to_count(uint8_t duty_percent)
{
    const uint32_t duty_period = 1U << SALTWATER_PUMP_PWM_RES_BITS;
    uint32_t duty = (duty_period * duty_percent) / 100U;
    if (duty >= duty_period) {
        duty = duty_period - 1U;
    }
    return duty;
}

esp_err_t saltwater_pump_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = SALTWATER_PUMP_PWM_MODE,
        .duty_resolution = SALTWATER_PUMP_PWM_RESOLUTION,
        .timer_num = SALTWATER_PUMP_PWM_TIMER,
        .freq_hz = SALTWATER_PUMP_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                        "configure saltwater pump PWM timer failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num = SALTWATER_PUMP_GPIO,
        .speed_mode = SALTWATER_PUMP_PWM_MODE,
        .channel = SALTWATER_PUMP_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SALTWATER_PUMP_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = SALTWATER_PUMP_OUTPUT_INVERT,
        },
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                        "configure saltwater pump PWM channel failed");

    s_initialized = true;
    ESP_LOGI(TAG, "GPIO%d PWM ready: %d Hz, duty=0%%",
             SALTWATER_PUMP_GPIO, SALTWATER_PUMP_PWM_FREQUENCY);
    return ESP_OK;
}

esp_err_t saltwater_pump_set_duty_percent(uint8_t duty_percent)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call saltwater_pump_init() first");
    ESP_RETURN_ON_FALSE(duty_percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "duty_percent must be between 0 and 100");

    ESP_RETURN_ON_ERROR(ledc_set_duty(SALTWATER_PUMP_PWM_MODE,
                                      SALTWATER_PUMP_PWM_CHANNEL,
                                      duty_percent_to_count(duty_percent)),
                        TAG, "set saltwater pump PWM duty failed");
    return ledc_update_duty(SALTWATER_PUMP_PWM_MODE,
                            SALTWATER_PUMP_PWM_CHANNEL);
}

esp_err_t saltwater_pump_stop(void)
{
    return saltwater_pump_set_duty_percent(0);
}

esp_err_t saltwater_pump_run_test(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call saltwater_pump_init() first");

    ESP_LOGI(TAG, "Saltwater pump test START: duty=%d%%, duration=%d ms",
             SALTWATER_PUMP_TEST_DUTY_PERCENT,
             SALTWATER_PUMP_TEST_DURATION_MS);
    ESP_RETURN_ON_ERROR(saltwater_pump_set_duty_percent(
                            SALTWATER_PUMP_TEST_DUTY_PERCENT),
                        TAG, "start saltwater pump test failed");

    vTaskDelay(pdMS_TO_TICKS(SALTWATER_PUMP_TEST_DURATION_MS));

    ESP_RETURN_ON_ERROR(saltwater_pump_stop(), TAG,
                        "stop saltwater pump test failed");
    ESP_LOGI(TAG, "Saltwater pump test DONE: GPIO%d duty=0%%",
             SALTWATER_PUMP_GPIO);
    return ESP_OK;
}
