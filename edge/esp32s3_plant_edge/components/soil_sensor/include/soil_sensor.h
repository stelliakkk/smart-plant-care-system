/*
 * 电容式土壤湿度传感器 (模拟输出 → ADC)
 * ---------------------------------------------------------------------
 * 电容式探头输出模拟电压: 越湿电压越低, 越干电压越高。
 * (电阻式探头也是这个规律, 但它靠电极导通测电导, 通电就电解腐蚀,
 *  插在土里几周就烂掉; 电容式不与土壤发生化学反应, 寿命长得多。)
 *
 * 本组件把整条链路包好, 上层只看到"湿度百分之多少":
 *     ADC 原始值  →  校准成真实电压(mV)  →  线性插值成湿度百分比
 *
 * 同时保留 raw 和 voltage_mv 两个中间量 —— 标定时要用它们。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 一次土壤读数(带中间量, 方便标定与排查) */
typedef struct {
    int   raw;          /* ADC 原始值 0 ~ 4095 (12 位) */
    int   voltage_mv;   /* 校准后的电压, 单位 mV */
    float percent;      /* 换算出的含水量 0 ~ 100 % */
} soil_reading_t;

/**
 * 标定端点(mV)
 * ---------------------------------------------------------------------
 * 电容式探头: 越干电压越高, 越湿电压越低。
 *   mv_dry  探头悬空在空气中时的电压(对应 0%)
 *   mv_wet  探头浸入水中时的电压(对应 100%)
 *
 * ★ 为什么做成参数而不是写死在驱动里:
 *   这两个值跟"探头批次 + 土质"强相关, 属于本机私有配置,
 *   应该放在 main/local_config.h 里。而 ESP-IDF 的分层规则不允许
 *   组件反向 include main/ 的头文件, 所以只能由上层调用时传进来。
 *   传 NULL 表示"用驱动内置的默认值"(仅保证能用, 不代表准)。
 */
typedef struct {
    int mv_dry;
    int mv_wet;
} soil_calibration_t;

/**
 * @brief 初始化 ADC 并建好校准
 *
 * 调用前无需其它前置(ADC 是片内外设)。内部会用 adc_oneshot_io_to_channel()
 * 把引脚号自动换算成 ADC 通道号和 ADC 单元号, 所以代码里只出现 GPIO4 一个数字。
 *
 * @param[in] cal 标定端点; 传 NULL 则使用驱动内置默认值
 *
 * @return ESP_OK 成功; ESP_ERR_INVALID_ARG 表示端点不合法(mv_dry <= mv_wet)
 */
esp_err_t soil_sensor_init(const soil_calibration_t *cal);

/**
 * @brief 读一次土壤湿度(内部连读 16 次取平均, 耗时几毫秒)
 *
 * @param[out] out 读数(含 raw / voltage_mv / percent)
 *
 * @return ESP_OK 成功; ESP_ERR_INVALID_STATE 表示尚未初始化
 */
esp_err_t soil_sensor_read(soil_reading_t *out);

#ifdef __cplusplus
}
#endif