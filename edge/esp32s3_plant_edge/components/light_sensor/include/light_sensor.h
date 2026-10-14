/*
 * BH1750 数字光照传感器 (I2C, 单位 lux)
 * ---------------------------------------------------------------------
 * 对外 3 个函数: 初始化 / 查状态 / 读一次光照。
 * 地址、命令字、等待时间、换算系数这些内部细节全藏在 .c 里。
 *
 * 为什么用"读一次"而不是"持续后台测量"?
 *   每秒采样一次的节奏下, 每次现测现读最简单也最好理解 —— 拿到的
 *   一定是刚测出来的值, 不会有"这数字是几秒前的吗"这种疑问。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 BH1750 (先探活, 再挂到 I2C 总线上)
 *
 * 内部会先发一次"探活"帧: 器件在位才会回 ACK。这样一旦接线有问题,
 * 报错信息会直接指向硬件(接线/上拉/地址), 而不是含糊的"读失败"。
 *
 * 调用前必须先完成 i2c_bus_init()。
 * 传感器拔掉时本函数返回失败, 但不会让芯片重启 —— 调用方可以只记个
 * 标志跳过光照采样, 其余传感器照常工作。
 *
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 表示总线上找不到器件
 */
esp_err_t light_sensor_init(void);

/**
 * @brief 传感器是否可用(初始化成功过)
 *
 * @return true = 可以调用 light_sensor_read_lux()
 */
bool light_sensor_is_ready(void);

/**
 * @brief 测一次光照强度(阻塞约 180ms)
 *
 * 每次调用都会触发一次全新测量, 因此返回的永远是新鲜值。
 *
 * @param[out] out_lux 光照强度, 单位 lux (0 ~ 54612)
 *
 * @return ESP_OK 成功; ESP_ERR_INVALID_STATE 表示尚未初始化
 */
esp_err_t light_sensor_read_lux(float *out_lux);

#ifdef __cplusplus
}
#endif