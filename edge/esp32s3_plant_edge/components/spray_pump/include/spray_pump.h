/*
 * GPIO13 农药喷雾泵 PWM 控制。
 * 初始化后默认保持 0%，不会自动启动水泵。
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t spray_pump_init(void);
esp_err_t spray_pump_set_duty_percent(uint8_t duty_percent);
esp_err_t spray_pump_stop(void);
#ifdef __cplusplus
}
#endif
