/*
 * 补水泵 PWM 驱动
 *
 * 对外只提供补水泵的行为接口，GPIO、LEDC 定时器和 PWM 参数都封装在
 * 组件内部，方便后续把一次性硬件测试改成正式的水泵控制逻辑。
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化补水泵 PWM，初始化完成后默认保持关闭。
 */
esp_err_t actuator_pump_init(void);

/**
 * @brief 设置补水泵 PWM 占空比。
 *
 * @param duty_percent 占空比，范围 0~100。
 */
esp_err_t actuator_pump_set_duty_percent(uint8_t duty_percent);

/**
 * @brief 关闭补水泵，将 PWM 占空比设为 0%。
 */
esp_err_t actuator_pump_stop(void);

/**
 * @brief 执行一次启动测试：默认 50% PWM，持续 5 秒后自动关闭。
 */
esp_err_t actuator_pump_run_test(void);

#ifdef __cplusplus
}
#endif
