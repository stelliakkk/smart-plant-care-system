/*
 * 板载 WS2812 可寻址 RGB 灯驱动 (基于 RMT 外设)
 * ---------------------------------------------------------------------
 * 对外只暴露 3 个函数, 内部实现 (位时序 / 编码回调 / RMT 通道) 完全隐藏。
 * 换句话说: 想点灯的人不需要、也不应该知道底下是 RMT。
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 WS2812 灯 (创建 RMT 发送通道 + 挂载编码器 + 使能)
 *
 * 调用成功后, 数据引脚即由 RMT 外设接管, 不要再手工操作该 GPIO。
 * 重复调用是安全的 (只会警告一次并直接返回)。
 *
 * @return ESP_OK 成功, 其他为失败
 */
esp_err_t rgb_led_init(void);

/**
 * @brief 把灯珠设置为指定颜色
 *
 * @param red   红色亮度 0(灭) ~ 255(最亮)
 * @param green 绿色亮度 0 ~ 255
 * @param blue  蓝色亮度 0 ~ 255
 *
 * @return ESP_OK 成功, ESP_ERR_INVALID_STATE 表示尚未初始化
 */
esp_err_t rgb_led_set(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief 熄灭灯珠 (等价于 rgb_led_set(0, 0, 0), 但意图更清楚)
 *
 * 注意: WS2812 内部有锁存电路, 必须主动发一帧全 0 才能真的熄灭,
 * 单纯"不再发数据"或复位芯片都不会让它灭。
 *
 * @return ESP_OK 成功, ESP_ERR_INVALID_STATE 表示尚未初始化
 */
esp_err_t rgb_led_off(void);

#ifdef __cplusplus
}
#endif
