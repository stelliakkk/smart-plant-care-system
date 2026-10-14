/*
 * GPIO11 盐水泵 PWM 控制。
 * 初始化后默认保持 0%，不会自动启动水泵。
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t saltwater_pump_init(void);
esp_err_t saltwater_pump_set_duty_percent(uint8_t duty_percent);
esp_err_t saltwater_pump_stop(void);
/* 启动验证：默认 50% PWM，持续 5 秒后自动关闭。 */
esp_err_t saltwater_pump_run_test(void);

#ifdef __cplusplus
}
#endif
