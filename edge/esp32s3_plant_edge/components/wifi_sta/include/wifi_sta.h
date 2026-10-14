/*
 * WiFi Station 连接 (连手机热点 / 家里路由器)
 * ---------------------------------------------------------------------
 * 对外 3 个函数: 初始化(开始连) / 等连上 / 查状态。
 * SSID 与密码从 app_config.h 里的 LOCAL_WIFI_* 取, 不写在本组件里 ——
 * 驱动组件不该关心"连哪个网", 那是应用层的事。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 WiFi 并开始连接(不阻塞)
 *
 * 账号密码由调用方传入, 而不是本组件自己去读配置文件 ——
 * 驱动层不该知道"连哪个网", 那是应用层的事(见 main/app_config.h)。
 * (另外 main/ 的头文件也禁止被其它组件 include, 所以只能这样传。)
 *
 * 内部依次做: NVS 初始化 → 建网络接口 → 建默认事件循环 → 起 WiFi 驱动
 * → 配置 STA 模式 → 启动。连上后自动获取 IP(DHCP)。
 *
 * 断线会自动重连, 不需要调用方操心。
 *
 * @param ssid     热点名(必须是 2.4GHz, ESP32 看不到 5GHz)
 * @param password 热点密码
 *
 * @return ESP_OK 成功(注意: 只代表"发起了连接", 不代表已连上)
 */
esp_err_t wifi_sta_init(const char *ssid, const char *password);

/**
 * @brief 阻塞等待"连上并拿到 IP"
 *
 * @param timeout_ms 超时(毫秒); 传 0 表示一直等
 *
 * @return ESP_OK 已连上; ESP_ERR_TIMEOUT 超时
 */
esp_err_t wifi_sta_wait_connected(uint32_t timeout_ms);

/**
 * @brief 当前是否已连上(拿到 IP)
 */
bool wifi_sta_is_connected(void);

/**
 * @brief 取当前热点的信号强度(给 status 心跳报文用)
 *
 * @return dBm, 负数; 越接近 0 越强(-50 很好 / -85 很差); 未连接时返回 0
 */
int8_t wifi_sta_get_rssi(void);

#ifdef __cplusplus
}
#endif