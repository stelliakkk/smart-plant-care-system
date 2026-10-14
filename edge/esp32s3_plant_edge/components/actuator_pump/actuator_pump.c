/*
 * ESP32-S3 补水泵 PWM 驱动
 * ---------------------------------------------------------------------
 * 默认配置用于首次硬件验证：GPIO9 输出 20kHz、50% 占空比，运行 5 秒
 * 后自动关闭。所有需要经常调整的参数集中在本文件顶部。
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"

#include "actuator_pump.h"

static const char *TAG = "ACTUATOR_PUMP";

/* -------------------------- 可调驱动参数 -------------------------- */

/* 补水泵 MOS 驱动输入脚。GPIO9 只连接 MOS 驱动输入，不直接连接水泵。 */
#define PUMP_GPIO                       9

/* 常见直流泵/MOS 驱动默认使用超出可闻频段的 PWM。 */
#define PUMP_PWM_FREQUENCY_HZ           20000

/* 10 位分辨率：50% 对应 512 个计数。 */
#define PUMP_DUTY_RESOLUTION            LEDC_TIMER_10_BIT
#define PUMP_DUTY_RESOLUTION_BITS       10

/* 上电硬件验证参数。 */
#define PUMP_TEST_DUTY_PERCENT          50
#define PUMP_TEST_DURATION_MS           5000

/* 0 = GPIO 高电平有效；1 = GPIO 低电平有效。 */
#define PUMP_OUTPUT_INVERT              0

#define PUMP_LEDC_MODE                  LEDC_LOW_SPEED_MODE
#define PUMP_LEDC_TIMER                 LEDC_TIMER_0
#define PUMP_LEDC_CHANNEL               LEDC_CHANNEL_0

static bool s_initialized = false;

static uint32_t duty_percent_to_count(uint8_t duty_percent)
{
    const uint32_t duty_period = 1U << PUMP_DUTY_RESOLUTION_BITS;
    uint32_t duty = (duty_period * duty_percent) / 100U;

    /* LEDC 在最高分辨率下无法可靠表达 duty_period 本身，100% 使用最大值。 */
    if (duty >= duty_period) {
        duty = duty_period - 1U;
    }
    return duty;
}

esp_err_t actuator_pump_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Init pump PWM: GPIO%d, %d Hz, %d-bit, output_invert=%d",
             PUMP_GPIO, PUMP_PWM_FREQUENCY_HZ, PUMP_DUTY_RESOLUTION_BITS,
             PUMP_OUTPUT_INVERT);

    const ledc_timer_config_t timer_config = {
        .speed_mode       = PUMP_LEDC_MODE,
        .duty_resolution  = PUMP_DUTY_RESOLUTION,
        .timer_num        = PUMP_LEDC_TIMER,
        .freq_hz          = PUMP_PWM_FREQUENCY_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_config), TAG,
                        "configure pump PWM timer failed");

    const ledc_channel_config_t channel_config = {
        .gpio_num   = PUMP_GPIO,
        .speed_mode = PUMP_LEDC_MODE,
        .channel    = PUMP_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = PUMP_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = PUMP_OUTPUT_INVERT,
        },
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel_config), TAG,
                        "configure pump PWM channel failed");

    s_initialized = true;
    ESP_LOGI(TAG, "Pump PWM initialized and stopped");
    return ESP_OK;
}

esp_err_t actuator_pump_set_duty_percent(uint8_t duty_percent)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call actuator_pump_init() first");
    ESP_RETURN_ON_FALSE(duty_percent <= 100, ESP_ERR_INVALID_ARG, TAG,
                        "duty_percent must be between 0 and 100");

    const uint32_t duty = duty_percent_to_count(duty_percent);
    ESP_RETURN_ON_ERROR(ledc_set_duty(PUMP_LEDC_MODE, PUMP_LEDC_CHANNEL, duty),
                        TAG, "set pump PWM duty failed");
    return ledc_update_duty(PUMP_LEDC_MODE, PUMP_LEDC_CHANNEL);
}

esp_err_t actuator_pump_stop(void)
{
    return actuator_pump_set_duty_percent(0);
}

esp_err_t actuator_pump_run_test(void)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call actuator_pump_init() first");

    ESP_LOGI(TAG, "Pump test START: duty=%d%%, duration=%d ms",
             PUMP_TEST_DUTY_PERCENT, PUMP_TEST_DURATION_MS);
    ESP_RETURN_ON_ERROR(actuator_pump_set_duty_percent(PUMP_TEST_DUTY_PERCENT),
                        TAG, "start pump test failed");

    vTaskDelay(pdMS_TO_TICKS(PUMP_TEST_DURATION_MS));

    ESP_RETURN_ON_ERROR(actuator_pump_stop(), TAG, "stop pump test failed");
    ESP_LOGI(TAG, "Pump test DONE: GPIO%d duty=0%%", PUMP_GPIO);
    return ESP_OK;
}
