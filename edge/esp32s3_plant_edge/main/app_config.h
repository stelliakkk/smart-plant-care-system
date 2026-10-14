/*
 * 应用层配置
 * ---------------------------------------------------------------------
 * 这里只放"应用行为"相关的参数。
 *
 * 分层边界提醒:
 *   驱动参数 (数据脚 GPIO 号 / RMT 时基 / 灯珠个数) 属于 rgb_led 组件
 *   内部实现, 不放这里 —— 应用层不该知道自己底下用的是 RMT 还是别的外设,
 *   这样将来换驱动 (比如换成 SPI 的 APA102) 时本文件一行都不用改。
 *   I2C 引脚(BH1750 光照) / ADC 引脚(电容式土壤探头) 同理, 分别藏在
 *   components/light_sensor 和 components/soil_sensor 里面。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------- 板载 RGB 灯演示开关 -------------------
 *   1 = 开机跑彩色循环演示: 红 -> 绿 -> 蓝 -> 青 -> 品红 -> 黄 -> 白 -> 灭
 *       (内部是死循环, 不会进入传感器采样 —— 专门用来看灯的)
 *   0 = 把灯锁存为"灭", 然后进入传感器采样循环   ← 当前默认
 * 改完重新 idf.py -p COM3 build flash 即可, 两种模式共用同一份代码。 */
#define RGB_LED_DEMO_ENABLE         0

/* 彩灯模式: 每隔多少毫秒换一种颜色 */
#define APP_DEMO_STEP_MS            500

/* 心跳日志周期 (毫秒) */
#define APP_HEARTBEAT_PERIOD_MS     1000

/* ==================== 边缘节点身份 ==================== */

/* 设备编号。
 * 必须和 backend/.env 里的 DEVICE_ID 一致(默认 edge01), 否则后端存的
 * device_id 对不上, 前端"查最新数据"就会一直显示"暂无数据"。 */
#define APP_DEVICE_ID               "edge01"

/* ==================== 采样与上报节奏 ==================== */

/* 传感器采样周期(毫秒): 每个周期读一次 BH1750 和土壤探头 */
#define APP_SAMPLE_PERIOD_MS        1000

/* MQTT 上报周期(毫秒): 每积够这么久就往服务器发一包 JSON。
 * 别调得太小 —— 后端每收到一包就往库里写一行, 报得太密会把历史表刷满。 */
#define APP_REPORT_PERIOD_MS        3000

/* ==================== 本机私有配置(不进 git) ====================
 *
 * WiFi 名称/密码、MQTT 服务器地址由根目录 start_dev.ps1 根据所选网络自动生成:
 *     main/generated_network_config.h  ← 已被 .gitignore 排除，切网后重新生成
 * 土壤探头标定仍属于本机私有，保存在 local_config.h。
 *
 * 没有 local_config.h 也能编译通过 —— 只是连不上网, 只能跑本地采样。
 * 这正是阶段 1 "先验串口、再上 WiFi" 想要的: 硬件没接好时也能先烧进去看读数。 */
#if __has_include("generated_network_config.h")
#include "generated_network_config.h"
#endif
#if __has_include("local_config.h")
#include "local_config.h"
#endif
#ifndef LOCAL_WIFI_SSID
#warning "未找到 generated_network_config.h: 请先运行根目录 start_dev.bat 选择网络；WiFi/MQTT 将用占位值"
#define LOCAL_WIFI_SSID            "CHANGE_ME"
#define LOCAL_WIFI_PASSWORD        "CHANGE_ME"
#define LOCAL_MQTT_BROKER_URI      "mqtt://192.168.1.100:1883"
#endif
#ifndef ACTIVE_NETWORK_PROFILE
#define ACTIVE_NETWORK_PROFILE     "unconfigured"
#endif
#ifndef LOCAL_SOIL_MV_DRY
/* 土壤标定端点的兜底值(经验值, 只保证能算出数, 不代表准) */
#define LOCAL_SOIL_MV_DRY          2600
#define LOCAL_SOIL_MV_WET          1200
#endif

#ifdef __cplusplus
}
#endif

/* Opt-in boot verification; remote-control firmware defaults to all outputs off. */
#ifndef APP_ACTUATOR_STARTUP_TEST_ENABLE
#define APP_ACTUATOR_STARTUP_TEST_ENABLE 0
#endif
