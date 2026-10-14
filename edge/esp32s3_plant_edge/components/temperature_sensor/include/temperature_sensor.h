/*
 * DS18B20 温度传感器驱动 (1-Wire)
 *
 * 注意：DS18B20 是数字 1-Wire 器件，GPIO8 不是 ADC 输入。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 GPIO8 1-Wire 总线并搜索第一个 DS18B20。 */
esp_err_t temperature_sensor_init(void);

/** 读取温度，单位为摄氏度。 */
esp_err_t temperature_sensor_read(float *temperature_c);

#ifdef __cplusplus
}
#endif
