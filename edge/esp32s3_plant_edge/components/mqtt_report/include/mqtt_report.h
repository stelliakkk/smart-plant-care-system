/*
 * MQTT 上报 (边缘端 → 后端)
 * ---------------------------------------------------------------------
 * 负责两件事: 连 broker、按"通信合同"发 telemetry / status 报文。
 *
 * 主题命名与字段定义见 pro/docs/mqtt_topics.md —— 那是两端的合同,
 * 改任何字段名之前, 先想清楚对面要不要一起改。
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 一次上报的传感器数据。
 *
 * 用 NAN(来自 math.h) 表示"这一项没有数据", 拼 JSON 时会写成 null。
 * 后端允许任何一项为 null, 所以某个传感器还没接上时,
 * 其余数据照样能正常上报入库 —— 这比"缺一项就整体不发"友好得多。
 */
typedef struct {
    float soil_moisture;   /* 土壤湿度 % */
    float soil_ec;         /* 土壤盐分 mS/cm (未接传感器时填 NAN) */
    float temperature;     /* 环境温度 °C   (未接传感器时填 NAN) */
    float light_lux;       /* 光照强度 lux  */
} telemetry_sample_t;

/**
 * @brief 初始化 MQTT 客户端并开始连接(不阻塞)
 *
 * broker 地址与设备编号由调用方传入, 本组件不读配置文件(理由同 wifi_sta)。
 *
 * 连不上会一直自动重试, 期间 publish 直接返回失败, 不会阻塞采样循环。
 *
 * @param broker_uri 形如 "mqtt://192.168.202.59:1883"
 * @param device_id  设备编号, 必须与后端 .env 的 DEVICE_ID 一致
 *
 * @return ESP_OK 成功(只代表"开始连了", 不代表已连上)
 */
esp_err_t mqtt_report_init(const char *broker_uri, const char *device_id);

/**
 * @brief 上报一次传感器数据 (主题 plant/{device_id}/telemetry, QoS 0)
 *
 * @param sample 读数; 其中没有数据的项填 NAN, 会被写成 null
 *
 * @return ESP_OK 已提交发送; ESP_ERR_INVALID_STATE 表示还没连上 broker
 */
esp_err_t mqtt_report_publish_telemetry(const telemetry_sample_t *sample);

/**
 * @brief 上报一次心跳 (主题 plant/{device_id}/status, QoS 0)
 *
 * 后端超过 30 秒收不到任何上行报文就判定设备离线, 所以心跳必须定期发。
 *
 * @param fault 自检异常说明, 正常时传 NULL 或空串。
 *              注意: 内容会直接进 JSON, 里面不能有双引号。
 *
 * @return ESP_OK 已提交发送; ESP_ERR_INVALID_STATE 表示还没连上 broker
 */
esp_err_t mqtt_report_publish_status(const char *fault);

/**
 * @brief 当前是否已连上 broker
 */
bool mqtt_report_is_connected(void);

#ifdef __cplusplus
}
#endif