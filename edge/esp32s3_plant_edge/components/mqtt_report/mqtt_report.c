/*
 * =====================================================================
 *  MQTT 上报 (边缘端 → 后端)
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1
 *  上行主题   : plant/{device_id}/telemetry  传感器数据
 *               plant/{device_id}/status     心跳(后端靠它判断在线)
 *
 *  ---------------------------------------------------------------
 *  避坑指南:
 *
 *  [1] ESP-IDF v6.1 里没有 mqtt 组件了
 *      esp-mqtt 已从内核移出, 现在靠 main/idf_component.yml 里声明的
 *      "espressif/mqtt" 托管组件提供, 编译时自动下载到 managed_components/。
 *      所以: 第一次编译必须联网, 否则 #include "mqtt_client.h" 找不到。
 *
 *  [2] QoS 用 0 而不是 1
 *      传感器数据每 3 秒一条, 丢一两条无所谓, 下一轮马上补上。用 QoS 1
 *      会带来确认报文和重发队列, 纯属多占内存和流量。
 *      (执行器指令 cmd 才必须 QoS 1 —— 那种报文一条都不能丢。)
 *
 *  [3] 浮点数在 JSON 里必须是 '.' 小数点
 *      C 输出小数用 printf 的 %f, 结果受 locale 影响。ESP-IDF 默认
 *      locale 是 "C"(小数点=点), 所以没问题; 但如果哪天有人调用
 *      setlocale() 改成欧洲语言, 就会输出 "24,1" 这种非法 JSON,
 *      后端解析直接失败。记住: 嵌入式里别动 locale。
 *
 *  [4] publish 的返回值 ≠ 送达
 *      esp_mqtt_client_publish() 返回的只是消息 id(>0 表示排队成功),
 *      不代表 broker 收到了。QoS 0 根本没有确认机制, 这是设计取舍,
 *      不是 bug。
 *
 *  [5] Windows 防火墙会挡住 broker
 *      板子连不上时先看是不是电脑的 1883 端口没放行(默认是挡的)。
 *      本项目的放行规则名: "PlantCare MQTT 1883"。
 * =====================================================================
 */

#include <stdatomic.h>
#include <math.h>       /* isnan */
#include <stdio.h>      /* snprintf */
#include <string.h>     /* strlcpy */

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"

#include "mqtt_report.h"
#include "wifi_sta.h"
#include "plant_control.h"

static const char *TAG = "MQTT";

/* ------------------------------ 内部参数 ------------------------------ */

/* 主题根: plant/{device_id}/{channel}, 与后端 topics.py 保持一致 */
#define TOPIC_ROOT          "plant"

/* 一行 JSON 大约 120 字节, 给 256 足够宽裕 */
#define PAYLOAD_BUF_SIZE    256

/* ------------------------------ 内部状态 ------------------------------ */

static esp_mqtt_client_handle_t s_client = NULL;
static char s_device_id[32] = { 0 };

/* volatile: 这两个变量会被 MQTT 事件回调(另一个任务)改写,
 * 主任务读的时候不能让编译器把它优化成缓存值。 */
static atomic_bool s_connected = false;

/* ------------------------------ 内部工具 ------------------------------ */

/**
 * @brief 把一个浮点数格式化成 JSON 数值片段
 *
 * 没有数据(传入 NAN)时输出 JSON 的 null —— 后端允许任何一项为 null,
 * 所以某个传感器没接时, 其余数据照样能正常上传。
 */
static void json_number(char *dst, size_t dst_size, float v, int digits)
{
    if (isnan(v)) {
        snprintf(dst, dst_size, "null");
        return;
    }
    snprintf(dst, dst_size, "%.*f", digits, (double)v);
}

/**
 * @brief 拼出 plant/{device_id}/{channel} 形式的主题
 */
static void build_topic(char *dst, size_t dst_size, const char *channel)
{
    snprintf(dst, dst_size, "%s/%s/%s", TOPIC_ROOT, s_device_id, channel);
}

/* ---------------------------- 事件回调 ---------------------------- */

static void publish_control_ack(const char *json) {
    if(!s_client || !s_connected) return;
    char topic[64]; build_topic(topic,sizeof(topic),"cmd_ack");
    esp_mqtt_client_enqueue(s_client,topic,json,0,1,0,false);
}
static void receive_fragment(esp_mqtt_event_handle_t evt) {
    static char buffer[1025];
    static int received, total;
    static bool retained, valid;
    if(evt->current_data_offset==0) {
        char topic[64]; build_topic(topic,sizeof(topic),"cmd");
        valid=evt->topic && evt->topic_len==(int)strlen(topic) && !memcmp(evt->topic,topic,evt->topic_len);
        received=0; total=evt->total_data_len; retained=evt->retain;
        if(total<=0 || total>1024) valid=false;
    }
    if(!valid || evt->current_data_offset!=received || evt->data_len<0 || received+evt->data_len>total) {valid=false;return;}
    memcpy(buffer+received,evt->data,evt->data_len); received+=evt->data_len;
    if(received==total) {buffer[total]=0;plant_control_receive(buffer,total,retained);valid=false;}
}
static void on_mqtt_event(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)args;
    (void)base;
    const esp_mqtt_event_handle_t evt = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        char command_topic[64]; build_topic(command_topic,sizeof(command_topic),"cmd");
        esp_mqtt_client_subscribe(s_client,command_topic,1);
        plant_control_connection(true);
        ESP_LOGI(TAG, "已连上 broker, device_id=%s", s_device_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        plant_control_connection(false);
        ESP_LOGW(TAG, "与 broker 断开, 客户端会自动重连");
        break;

    case MQTT_EVENT_ERROR:
        s_connected = false;
        plant_control_connection(false);
        /* error_type: 1=TCP 连不上(地址/端口/防火墙), 2=broker 拒绝(认证/权限) */
        if (evt != NULL && evt->error_handle != NULL) {
            ESP_LOGE(TAG, "MQTT 出错 error_type=%d (1=连不上, 2=broker 拒绝; "
                          "先确认 broker 在跑且 1883 已放行)",
                     evt->error_handle->error_type);
        } else {
            ESP_LOGE(TAG, "MQTT 出错");
        }
        break;

    case MQTT_EVENT_DATA:
        receive_fragment(evt);
        break;
    default:
        break;
    }
}

/* ------------------------------ 实现 ------------------------------ */

esp_err_t mqtt_report_init(const char *broker_uri, const char *device_id)
{
    ESP_RETURN_ON_FALSE(broker_uri != NULL && device_id != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "broker_uri/device_id is NULL");
    ESP_RETURN_ON_FALSE(s_client == NULL, ESP_ERR_INVALID_STATE, TAG,
                        "already initialized");

    strlcpy(s_device_id, device_id, sizeof(s_device_id));

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_uri,
        /* keepalive: 每隔这么久给 broker 发一次 PING, 证明"我还活着"。
         * 后端判离线是 30 秒, 而我们每 3 秒就有一条 status, 足够密。 */
        .session.keepalive = 30,
        /* 断线后隔多久重连一次(毫秒) */
        .network.reconnect_timeout_ms = 3000,
    };

    ESP_LOGI(TAG, "初始化 MQTT 客户端: broker=%s, device_id=%s", broker_uri, s_device_id);

    ESP_RETURN_ON_ERROR(plant_control_init(publish_control_ack), TAG, "control init failed");
    s_client = esp_mqtt_client_init(&cfg);
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_FAIL, TAG, "esp_mqtt_client_init failed");

    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                       on_mqtt_event, NULL),
                        TAG, "register mqtt event failed");
    ESP_RETURN_ON_ERROR(esp_mqtt_client_start(s_client), TAG, "start mqtt client failed");

    /* start 是异步的: 真正连上要等 MQTT_EVENT_CONNECTED 事件回来。
     * 这里不阻塞, 采样循环照常跑, 连不上就后台一直重试。 */
    return ESP_OK;
}

esp_err_t mqtt_report_publish_telemetry(const telemetry_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG, "sample is NULL");
    ESP_RETURN_ON_FALSE(s_client != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    /* 没连上就安静跳过: 每秒刷一条"没连上"只会把日志淹掉,
     * 真正有用的是连接状态变化时那条 WARN/ERROR。 */
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 先把每个数值单独格式化成 JSON 片段, 再整体拼接 ——
     * 这样"某一项没数据"时只把那一项写成 null, 不影响其它字段。 */
    char moisture[24], ec[24], temp[24], lux[24];
    json_number(moisture, sizeof(moisture), sample->soil_moisture, 1);
    json_number(ec,       sizeof(ec),       sample->soil_ec,       2);
    json_number(temp,     sizeof(temp),     sample->temperature,   1);
    json_number(lux,      sizeof(lux),      sample->light_lux,     1);

    char payload[PAYLOAD_BUF_SIZE];
    const int n = snprintf(payload, sizeof(payload),
                           "{\"device_id\":\"%s\","
                           "\"soil_moisture\":%s,"
                           "\"soil_ec\":%s,"
                           "\"temperature\":%s,"
                           "\"light_lux\":%s}",
                           s_device_id, moisture, ec, temp, lux);

    /* snprintf 返回"本来想写多少字节"。一旦 >= 缓冲区大小就说明被截断了,
     * 那是半截 JSON —— 宁可不发, 也不能发出去让后端解析失败。 */
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        ESP_LOGE(TAG, "telemetry JSON 超出 %d 字节缓冲, 本次放弃上报",
                 (int)sizeof(payload));
        return ESP_ERR_INVALID_SIZE;
    }

    char topic[64];
    build_topic(topic, sizeof(topic), "telemetry");

    const int msg_id = esp_mqtt_client_publish(s_client, topic, payload,
                                               n,    /* 长度(显式给, 免得再算一次 strlen) */
                                               0,    /* QoS 0 */
                                               0);   /* retain = 0 */
    if (msg_id < 0) {
        ESP_LOGW(TAG, "上报失败(发送队列已满?), 本条丢弃");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "已上报 %s", payload);
    return ESP_OK;
}

esp_err_t mqtt_report_publish_status(const char *fault)
{
    if(!s_client || !s_connected) return ESP_ERR_INVALID_STATE;
    cJSON *root=cJSON_CreateObject();
    if(!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root,"device_id",s_device_id);
    cJSON_AddBoolToObject(root,"online",true);
    cJSON_AddNumberToObject(root,"rssi",wifi_sta_get_rssi());
    cJSON_AddNumberToObject(root,"uptime_s",esp_timer_get_time()/1000000);
    cJSON_AddStringToObject(root,"fault",fault?fault:"");
    plant_control_status(root);
    char *payload=cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(!payload) return ESP_ERR_NO_MEM;
    char topic[64];build_topic(topic,sizeof(topic),"status");
    int id=esp_mqtt_client_publish(s_client,topic,payload,0,0,0);
    cJSON_free(payload);
    return id<0?ESP_FAIL:ESP_OK;
}
bool mqtt_report_is_connected(void)
{
    return s_connected;
}


