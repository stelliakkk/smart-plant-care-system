/*
 * GPIO15 补光灯 PWM 控制。
 * 初始化后默认保持 0%，不会自动点亮补光灯。
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t grow_light_init(void);
esp_err_t grow_light_set_duty_percent(uint8_t duty_percent);
esp_err_t grow_light_stop(void);
/* 启动验证：全亮 5 秒后自动熄灭。 */
esp_err_t grow_light_run_test(void);

#ifdef __cplusplus
}
#endif
