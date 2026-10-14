/*
 * =====================================================================
 *  WiFi Station 连接
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1
 *  连接对象   : main/local_config.h 里的 LOCAL_WIFI_SSID / LOCAL_WIFI_PASSWORD
 *
 *  ---------------------------------------------------------------
 *  避坑指南:
 *
 *  [1] ESP32 只支持 2.4GHz!
 *      手机热点若有 5GHz 频段, 板子根本看不到它, 现象是"一直重连、
 *      reason=201(找不到 AP)"。到热点设置里切到 2.4G 再试。
 *
 *  [2] NVS 必须先初始化
 *      WiFi 驱动要把射频校准数据存进 NVS。没跑 nvs_flash_init() 就直接
 *      esp_wifi_init(), 会返回 ESP_ERR_NVS_NOT_INITIALIZED。
 *
 *  [3] 事件回调里别做耗时的事
 *      回调跑在系统事件循环任务里(栈很小), 在里面 vTaskDelay 或跑大循环
 *      会把整条事件链堵死, 表现为"WiFi 状态卡住不动"。
 *      所以这里回调只做"置标志 / 触发重连 / 打一行日志"。
 *
 *  [4] 断线重连要自己写
 *      WiFi 驱动只负责"断线时通知你", 不会自动重连。必须在
 *      WIFI_EVENT_STA_DISCONNECTED 回调里再调一次 esp_wifi_connect(),
 *      否则网络一抖就永久掉线了。
 * =====================================================================
 */

#include <string.h>          /* strlcpy */

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "wifi_sta.h"

static const char *TAG = "WIFI";

/* ------------------------------ 内部状态 ------------------------------ */

/* 事件组: 让"主任务"能阻塞等待"连上了"这件事, 而不用写轮询 */
#define WIFI_CONNECTED_BIT   BIT0

static EventGroupHandle_t s_events = NULL;
static bool s_connected = false;
static int  s_retry_count = 0;

/* 存一份 SSID 副本: 事件回调里要打日志, 而回调拿不到 init 的局部变量。
 * 33 字节 = 32(SSID 上限) + 1(结束符), 是 WiFi 协议规定的最大长度。 */
static char s_ssid[33] = { 0 };

/* ---------------------------- 事件回调 ---------------------------- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;      /* 本回调只注册给 WIFI_EVENT, 用 id 判断就够了 */

    if (id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "开始连接热点 \"%s\" ...", s_ssid);
        esp_wifi_connect();

    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *evt = (const wifi_event_sta_disconnected_t *)data;

        s_connected = false;
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        s_retry_count++;

        /* 记住这几个 reason, 排查时能省掉大量时间 */
        const char *hint = "";
        switch (evt->reason) {
        case WIFI_REASON_AUTH_FAIL:
            hint = " <- 密码不对"; break;
        case WIFI_REASON_NO_AP_FOUND:
            hint = " <- 搜不到该热点(名字拼错 / 是 5GHz / 超出范围)"; break;
        case WIFI_REASON_ASSOC_FAIL:
            hint = " <- 关联被拒(热点可能开了 MAC 白名单或已满员)"; break;
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            hint = " <- 握手超时(密码错或信号太差)"; break;
        default:
            break;
        }
        ESP_LOGW(TAG, "WiFi 断开 reason=%d%s, 第 %d 次重连...",
                 evt->reason, hint, s_retry_count);

        /* 关键: 不重连的话, 断一次就永远断着 */
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        s_connected = true;
        s_retry_count = 0;
        ESP_LOGI(TAG, "已连上! IP=" IPSTR "  网关=" IPSTR,
                 IP2STR(&evt->ip_info.ip), IP2STR(&evt->ip_info.gw));
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

/* ------------------------------ 实现 ------------------------------ */

esp_err_t wifi_sta_init(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid != NULL && password != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "ssid/password is NULL");

    /* 存一份副本给事件回调打日志用 */
    strlcpy(s_ssid, ssid, sizeof(s_ssid));

    /* 1) NVS —— WiFi 驱动拿它存射频校准数据 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* 分区用满或版本不匹配: 擦掉重建是官方推荐做法 */
        ESP_LOGW(TAG, "NVS 需要擦除重建 (%s)", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init failed");

    /* 2) 网络接口 + 默认事件循环。
     *    esp_event_loop_create_default() 重复调用会返回 ESP_ERR_INVALID_STATE,
     *    那表示别人已经建过了 —— 属于正常情况, 不能当错误处理。 */
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "create default event loop failed");
    }
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta() != NULL,
                        ESP_FAIL, TAG, "create default wifi sta netif failed");

    /* 3) 起 WiFi 驱动(默认配置里已含合理的缓冲数量与任务优先级) */
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "esp_wifi_init failed");

    /* 4) 事件组 + 注册回调 */
    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events != NULL, ESP_ERR_NO_MEM, TAG, "create event group failed");

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL),
                        TAG, "register WIFI_EVENT handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL),
                        TAG, "register IP_EVENT handler failed");

    /* 5) 填账号密码。
     *    ssid/password 是 uint8_t 数组, 用 strlcpy 保证末尾有 '\0' 且不越界
     *    (用 strcpy 的话, 密码一旦比数组长就会踩内存)。
     *
     *    ★ 故意不设 threshold.authmode: 让它接受 WPA/WPA2/WPA3 各种热点。
     *      若写死 WIFI_AUTH_WPA2_PSK, 反而连不上默认开 WPA3 的 iPhone 热点。 */
    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG,
                        "set wifi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    /* esp_wifi_start() 是异步的: 它只负责"启动", 真正发起连接要等
     * WIFI_EVENT_STA_START 事件回来。所以这里不阻塞, 交给 wait_connected。 */
    return ESP_OK;
}

esp_err_t wifi_sta_wait_connected(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_events != NULL, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    const TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    const EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT,
                                                 pdFALSE,   /* 不清除位, 之后还要查状态 */
                                                 pdTRUE,    /* 等指定位置齐 */
                                                 ticks);
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool wifi_sta_is_connected(void)
{
    return s_events != NULL && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0;
}

int8_t wifi_sta_get_rssi(void)
{
    if (!s_connected) {
        return 0;
    }

    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return 0;
    }
    /* ap.rssi 就是 dBm, 负数 */
    return ap.rssi;
}
