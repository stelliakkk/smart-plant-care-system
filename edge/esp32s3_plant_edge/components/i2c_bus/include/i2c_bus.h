/*
 * I2C 主机总线
 * ---------------------------------------------------------------------
 * 对外只暴露 2 个函数: 建总线、取句柄。
 * 引脚号 / 速率 / 滤波深度这些细节全藏在 i2c_bus.c 里 —— 换接线时
 * 上层一个文件都不用动。
 *
 * 为什么要有这么薄的一层?
 *   因为将来还会接第 2、第 3 个 I2C 器件(比如 OLED、SHT30)。总线只能
 *   建一次, 让每个传感器各自去建会互相打架; 统一由这里建、别人来挂,
 *   是最省事也最不容易出错的分工。
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 总线速率: 挂在这条总线上的所有器件都用它。
 * 放头文件里是为了避免各传感器组件各自写死一个数字, 将来改的时候漏掉。 */
#define I2C_BUS_SPEED_HZ        100000

/**
 * @brief 初始化 I2C 主机总线 (SCL=GPIO5, SDA=GPIO6, 100kHz)
 *
 * 整个工程只需调用一次。之后各传感器组件用 i2c_bus_get_handle() 取到
 * 句柄, 再调 i2c_master_bus_add_device() 把自己挂上去。
 *
 * 重复调用是安全的(只警告一次并直接返回), 不会重复占用 I2C 外设。
 *
 * @return ESP_OK 成功, 其他为失败
 */
esp_err_t i2c_bus_init(void);

/**
 * @brief 取总线句柄(给各传感器组件挂设备用)
 *
 * @return 总线句柄; 尚未初始化时返回 NULL
 */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

#ifdef __cplusplus
}
#endif