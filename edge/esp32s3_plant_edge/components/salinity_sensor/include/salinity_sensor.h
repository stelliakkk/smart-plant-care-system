/*
 * 土壤盐分传感器驱动 (模拟输出 -> ADC)
 *
 * 本组件只负责稳定地读取 ADC 原始值和电压，不假设具体传感器型号，
 * 因此暂不把电压换算成 EC 或盐分浓度。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int raw;         /* ADC 原始值 */
    int voltage_mv;  /* 校准后的电压，单位 mV */
} salinity_reading_t;

/** 初始化 GPIO7 对应的 ADC 通道。 */
esp_err_t salinity_sensor_init(void);

/** 读取一次盐分传感器，内部多次采样取平均。 */
esp_err_t salinity_sensor_read(salinity_reading_t *out);

#ifdef __cplusplus
}
#endif
