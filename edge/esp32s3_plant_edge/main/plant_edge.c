/*
 * =====================================================================
 *  ESP32-S3-WROOM-1 入门示例 —— 应用编排层
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1 (模组 ESP32-S3-WROOM-1)
 *  ESP-IDF    : v6.1
 *
 *  功能:
 *    1) 开机打印芯片信息 (型号/核心数/硅片版本/特性/MAC/Flash/复位原因)
 *    2) 驱动板载可寻址 RGB 灯 (WS2812), 循环显示红/绿/蓝/青/品红/黄/白/灭
 *    3) 每秒读一次光照(BH1750)与土壤湿度(电容式探头)
 *    4) ★ 每 3 秒通过 MQTT 把读数上报给后端 -> 入库 -> 前端页面刷新
 *
 *  ---------------------------------------------------------------
 *  当前进度: 阶段 2 —— "传感器 → MQTT → 后端 → 页面" 全链路打通
 *    阶段 1 已单独验证过传感器(串口读数正常), 现在才把网络接上来。
 *    分两步走的好处: 一旦出问题, 能立刻判断是"传感器的事"还是"网络的事"。
 *
 *  数据流:
 *    传感器 → ESP32 采样 → MQTT plant/edge01/telemetry → 后端入库
 *      → 前端每 3 秒轮询 /api/telemetry/latest → 页面数字跟着动
 *
 *  ---------------------------------------------------------------
 *  本文件只负责"编排": 依次调用各模块, 自己不含任何硬件细节。
 *  想了解硬件细节, 请到对应组件里看:
 *    components/rgb_led/     WS2812 位时序 / GRB 字节序 / RMT 通道 (脚位 GPIO38)
 *    components/board_info/  芯片信息 / MAC / Flash 容量 / 复位原因
 *  想调应用行为 (演示开关、心跳周期), 请改 main/app_config.h
 *
 *  ---------------------------------------------------------------
 *  模块化带来的好处 (就拿本工程举例):
 *    - components/rgb_led/ 整包复制到别的工程即可使用, 只需改文件里
 *      一个 RGB_LED_GPIO 宏;
 *    - 将来若要换成另一种灯 (例如 SPI 接口的 APA102), 只重写 rgb_led
 *      组件内部, 本文件和 app_config.h 一行都不用动;
 *    - 依赖是单向的: main -> 组件, 组件之间互不依赖, 不会绕成一团乱麻。
 * =====================================================================
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

/* 本工程自己的模块 */
#include "app_config.h"
#include "actuator_pump.h"
#include "board_info.h"
#include "grow_light.h"
#include "i2c_bus.h"
#include "light_sensor.h"
#include "mqtt_report.h"
#include "rgb_led.h"
#include "salinity_sensor.h"
#include "saltwater_pump.h"
#include "spray_pump.h"
#include "soil_sensor.h"
#include "temperature_sensor.h"
#include "wifi_sta.h"

#include "plant_control.h"
#include <math.h>       /* NAN: 表示"这一项没有数据" */
#include <stdio.h>      /* snprintf: 把读数拼成一行日志 */

static const char *TAG = "APP";

/* ---------------------------- 演示用颜色表 ---------------------------- */

typedef struct {
    const char *name;
    uint8_t r, g, b;
} led_color_t;

/* 只在本文件内使用, 所以加 static 限定作用域, 不污染其他模块的符号表 */
static const led_color_t k_color_table[] = {
    { "RED",     255,   0,   0 },
    { "GREEN",     0, 255,   0 },
    { "BLUE",      0,   0, 255 },
    { "CYAN",      0, 255, 255 },
    { "MAGENTA", 255,   0, 255 },
    { "YELLOW",  255, 255,   0 },
    { "WHITE",   255, 255, 255 },
    { "OFF",       0,   0,   0 },
};
#define COLOR_NUM   ((int)(sizeof(k_color_table) / sizeof(k_color_table[0])))

/* ------------------------- 模式一: 彩色循环演示 ------------------------- */

/* 本函数不会返回 (内部是死循环), 属于"任务型"函数的常见写法 */
static void run_led_demo(void)
{
    ESP_LOGI(TAG, "=== LED demo mode ON: color changes every %d ms ===", APP_DEMO_STEP_MS);

    int color_idx = 0;
    int64_t next_beat_us = esp_timer_get_time() + (int64_t)APP_HEARTBEAT_PERIOD_MS * 1000;

    while (1) {
        const led_color_t *c = &k_color_table[color_idx];

        /* 切换灯的颜色 */
        ESP_ERROR_CHECK(rgb_led_set(c->r, c->g, c->b));

        /* 心跳: 每满一个周期打印一次 */
        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_beat_us) {
            ESP_LOGI(TAG, "uptime = %4lld s | free heap = %6u B | LED = %-7s (R=%3u G=%3u B=%3u)",
                     (long long)(now_us / 1000000),
                     (unsigned)esp_get_free_heap_size(),
                     c->name, c->r, c->g, c->b);
            next_beat_us += (int64_t)APP_HEARTBEAT_PERIOD_MS * 1000;
            /* 万一被别的事情拖慢很久, 避免一次补打好几拍 */
            if (now_us > next_beat_us) {
                next_beat_us = now_us + (int64_t)APP_HEARTBEAT_PERIOD_MS * 1000;
            }
        }

        color_idx = (color_idx + 1) % COLOR_NUM;
        vTaskDelay(pdMS_TO_TICKS(APP_DEMO_STEP_MS));   /* 每组颜色亮 500ms */
    }
}

/* 说明: 原来还有一个"灭灯 + 每秒心跳"的模式函数 run_led_off(), 它内部是
 * 死循环。现在传感器采样循环自己会每秒打印一行(带 heap 占用), 心跳功能
 * 已经被它取代, 所以删掉那个函数 —— 免得留下"定义了却没人调用"的死代码,
 * 编译器也会报 -Wunused-function 警告。 */

/* ------------------------ 传感器采样与上报 ------------------------ */

/**
 * 一轮采样的结果。
 *
 * 用 NAN 表示"这一项没读到", 和上报 JSON 里的 null 一一对应 ——
 * 中间不需要额外的 has_xxx 标志位, 少一层出错的可能。
 */
typedef struct {
    float light_lux;      /* 光照 lux; NAN = 没读到 */
    float soil_percent;   /* 土壤湿度 %; NAN = 没读到 */
    int   soil_raw;       /* ADC 原始值(日志里用来看标定情况) */
    int   soil_mv;        /* 电压 mV(同上) */
    int   salinity_raw;   /* 盐分 ADC 原始值 */
    int   salinity_mv;    /* 盐分传感器电压 mV */
    float temperature_c;  /* DS18B20 温度 °C */
} sample_result_t;

/**
 * @brief 读一轮两个传感器, 结果填进 out。
 *
 * @param light_ready 光照传感器是否可用(读失败不会把它置为不可用, 下轮自动重试)
 * @param soil_ready  土壤传感器是否可用
 * @param out         采样结果
 */
static void sample_once(const bool *light_ready, const bool *soil_ready,
                        const bool *salinity_ready, const bool *temperature_ready,
                        sample_result_t *out)
{
    out->light_lux = NAN;
    out->soil_percent = NAN;
    out->soil_raw = 0;
    out->soil_mv = 0;
    out->salinity_raw = 0;
    out->salinity_mv = 0;
    out->temperature_c = NAN;

    /* ---------- 光照 (BH1750, 内部阻塞约 180ms 等测量完成) ---------- */
    if (*light_ready) {
        float lux = 0.0f;
        const esp_err_t err = light_sensor_read_lux(&lux);
        if (err == ESP_OK) {
            out->light_lux = lux;
        } else {
            /* 读失败只告警不 abort: 杜邦线松动是常有的事,
             * 下个周期会自动重试, 插好就自己恢复了。 */
            ESP_LOGW(TAG, "读光照失败: %s", esp_err_to_name(err));
        }
    }

    /* ---------- 土壤 (ADC, 内部连读 16 次取平均) ---------- */
    if (*soil_ready) {
        soil_reading_t soil = { 0, 0, 0.0f };
        const esp_err_t err = soil_sensor_read(&soil);
        if (err == ESP_OK) {
            out->soil_percent = soil.percent;
            out->soil_raw = soil.raw;
            out->soil_mv = soil.voltage_mv;
        } else {
            ESP_LOGW(TAG, "读土壤失败: %s", esp_err_to_name(err));
        }
    }

    /* ---------- 盐分 (GPIO7 ADC, 内部连读取平均) ---------- */
    if (*salinity_ready) {
        salinity_reading_t salinity = { 0, 0 };
        const esp_err_t err = salinity_sensor_read(&salinity);
        if (err == ESP_OK) {
            out->salinity_raw = salinity.raw;
            out->salinity_mv = salinity.voltage_mv;
        } else {
            ESP_LOGW(TAG, "读盐分传感器失败: %s", esp_err_to_name(err));
        }
    }

    /* ---------- 温度 (GPIO8 DS18B20 1-Wire) ---------- */
    if (*temperature_ready) {
        float temperature_c = 0.0f;
        const esp_err_t err = temperature_sensor_read(&temperature_c);
        if (err == ESP_OK) {
            out->temperature_c = temperature_c;
        } else {
            ESP_LOGW(TAG, "读 DS18B20 失败: %s", esp_err_to_name(err));
        }
    }
    plant_control_light_sample(out->light_lux);
}

/**
 * @brief 把一轮采样打印成一行日志。
 *
 * 两个数值都先格式化成定宽字符串再拼: 每行的列都能对齐, 眼睛扫一眼
 * 就能看出"哪个数字变了、变了多少"。
 * 注意 printf 里想输出百分号本身, 必须写 "%%"。
 */
static void log_sample(const sample_result_t *r)
{
    char light_str[24];
    char soil_str[48];
    char salinity_str[40];
    char temperature_str[24];

    if (isnan(r->light_lux)) {
        snprintf(light_str, sizeof(light_str), "     -- lux");
    } else {
        snprintf(light_str, sizeof(light_str), "%7.1f lux", (double)r->light_lux);
    }

    if (isnan(r->soil_percent)) {
        snprintf(soil_str, sizeof(soil_str), "raw=  --    -- mV    -- %%");
    } else {
        snprintf(soil_str, sizeof(soil_str), "raw=%4d %4d mV %5.1f %%",
                 r->soil_raw, r->soil_mv, (double)r->soil_percent);
    }

    snprintf(salinity_str, sizeof(salinity_str), "raw=%4d %4d mV",
             r->salinity_raw, r->salinity_mv);
    if (isnan(r->temperature_c)) {
        snprintf(temperature_str, sizeof(temperature_str), "    -- C");
    } else {
        snprintf(temperature_str, sizeof(temperature_str), "%6.2f C",
                 (double)r->temperature_c);
    }

    ESP_LOGI(TAG, "light=%s | soil=%s | salinity=%s | temp=%s | heap=%u B",
             light_str, soil_str, salinity_str, temperature_str,
             (unsigned)esp_get_free_heap_size());
}

/**
 * @brief 把一轮采样上报给后端。
 *
 * 盐分和温度这两项是留给将来接入的传感器 —— 现在没有数据就传 NAN,
 * 拼成 JSON 就是 null。后端允许任何一项为 null, 所以不会因为
 * "传感器还没买"就报错或丢弃整条数据。
 */
static void publish_sample(const sample_result_t *r)
{
    const telemetry_sample_t t = {
        .soil_moisture = r->soil_percent,
        .soil_ec       = NAN,   /* 盐分传感器: 待接入 */
        .temperature   = NAN,   /* 温度传感器: 待接入 */
        .light_lux     = r->light_lux,
    };

    mqtt_report_publish_telemetry(&t);

    /* 心跳单独发一条: 后端靠"最近一次上行报文的时间"判断设备是否在线,
     * 所以即使传感器全部读失败, 心跳也该照发 —— 让后端知道
     * "板子活着, 只是传感器有点问题"。 */
    mqtt_report_publish_status(NULL);
}

/* ------------------------------ 主程序 ------------------------------ */

void app_main(void)
{
    /* 1) 先把芯片家底打印出来, 方便排查问题 */
    board_info_print();

    /* 2) 补水泵硬件验证。
     *    默认 GPIO9 输出 20kHz、50% PWM 5 秒, 然后自动关闭。
     *    这样先验证 MOS 驱动和水泵硬件, 后续可复用组件接口接入业务控制。 */
    ESP_ERROR_CHECK(actuator_pump_init());
#if APP_ACTUATOR_STARTUP_TEST_ENABLE
    ESP_ERROR_CHECK(actuator_pump_run_test());
#endif

    /* 3) 板载 RGB 灯。
     *    彩灯演示模式内部是死循环(不会返回), 所以放在传感器初始化之前:
     *    打开演示时就专心看灯, 关掉时才进入传感器采样。 */
    ESP_ERROR_CHECK(rgb_led_init());
    if (RGB_LED_DEMO_ENABLE) {
        run_led_demo();      /* 本函数不会返回 */
    }
    /* 关键: WS2812 灯珠内部有锁存电路, 会一直保持最后收到的颜色!
     * "不再发数据"和"复位芯片"都不会让它熄灭, 必须主动发一帧全 0 的
     * 24bit 数据, 才能把它内部的记忆改写成"全灭"。 */
    ESP_ERROR_CHECK(rgb_led_off());
    ESP_LOGI(TAG, "RGB LED = OFF (latched)");

    /* 4) 传感器初始化。
     *    ★ 这里故意不用 ESP_ERROR_CHECK: 传感器是可以被拔掉的, 一拔就
     *    重启会让人误以为"板子坏了/程序崩了"。所以失败只记标志 + 打告警,
     *    采样时跳过它, 其余传感器照常工作 —— 这正是"初始化失败要拦,
     *    运行期失败不拦"的分寸。 */
    esp_err_t err = i2c_bus_init();
    const bool bus_ok = (err == ESP_OK);
    if (!bus_ok) {
        ESP_LOGE(TAG, "I2C 总线初始化失败: %s", esp_err_to_name(err));
    }

    bool light_ready = false;
    if (bus_ok) {
        err = light_sensor_init();
        light_ready = (err == ESP_OK);
        if (!light_ready) {
            ESP_LOGW(TAG, "光照传感器不可用, 本次只跑土壤采样");
        }
    }

    /* 标定端点属于"本机私有"(换探头 / 换土就得重标), 放在 main/local_config.h,
     * 由这里喂给组件 —— ESP-IDF 的分层规则不允许组件反向 include main/ 的头文件,
     * 所以只能像 WiFi 参数那样"从上往下传"。 */
    const soil_calibration_t soil_cal = {
        .mv_dry = LOCAL_SOIL_MV_DRY,
        .mv_wet = LOCAL_SOIL_MV_WET,
    };

    bool soil_ready = true;
    err = soil_sensor_init(&soil_cal);
    if (err != ESP_OK) {
        soil_ready = false;
        ESP_LOGW(TAG, "土壤传感器不可用: %s", esp_err_to_name(err));
    }

    bool salinity_ready = true;
    err = salinity_sensor_init();
    if (err != ESP_OK) {
        salinity_ready = false;
        ESP_LOGW(TAG, "盐分传感器不可用: %s", esp_err_to_name(err));
    }

    bool temperature_ready = true;
    err = temperature_sensor_init();
    if (err != ESP_OK) {
        temperature_ready = false;
        ESP_LOGW(TAG, "DS18B20 温度传感器不可用: %s", esp_err_to_name(err));
    }

    /* 盐水泵、农药泵和补光灯依次进行启动硬件验证，验证后自动关闭。 */
    ESP_ERROR_CHECK(saltwater_pump_init());
#if APP_ACTUATOR_STARTUP_TEST_ENABLE
    ESP_ERROR_CHECK(saltwater_pump_run_test());
#endif
    ESP_ERROR_CHECK(spray_pump_init());
#if APP_ACTUATOR_STARTUP_TEST_ENABLE
    ESP_ERROR_CHECK(spray_pump_run_test());
#endif
    ESP_ERROR_CHECK(grow_light_init());
#if APP_ACTUATOR_STARTUP_TEST_ENABLE
    ESP_ERROR_CHECK(grow_light_run_test());
#endif

    if (!light_ready && !soil_ready && !salinity_ready && !temperature_ready) {
        ESP_LOGE(TAG, "所有传感器都没就绪 —— 先查接线(pro/docs/hardware_wiring.md)");
    }

    /* 5) 联网 + MQTT。
     *    同样不用 ESP_ERROR_CHECK: 网络不通时的正确行为是"继续本地采样,
     *    后台不停重连", 而不是把整个程序 abort 掉。
     *
     *    wait_connected 给 15 秒: 热点一般 2~4 秒就能连上, 等太久会让人
     *    以为卡死了; 等不到就先往下走, 反正 WiFi 会自动重连。 */
    ESP_LOGI(TAG, "正在连接 WiFi: profile=%s, ssid=%s", ACTIVE_NETWORK_PROFILE,
             LOCAL_WIFI_SSID);
    err = wifi_sta_init(LOCAL_WIFI_SSID, LOCAL_WIFI_PASSWORD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 初始化失败: %s (本地采样继续)", esp_err_to_name(err));
    } else {
        if (wifi_sta_wait_connected(15000) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi 就绪");
        } else {
            ESP_LOGW(TAG, "15 秒内没连上, 先继续本地采样, 后台会一直重连");
        }

        /* broker 地址里的 IP 必须和电脑当前 IP 一致(见 main/local_config.h) */
        err = mqtt_report_init(LOCAL_MQTT_BROKER_URI, APP_DEVICE_ID);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MQTT 初始化失败: %s (本地采样继续)", esp_err_to_name(err));
        }
    }

    /* 6) 采样 + 上报主循环。
     *    两个独立的节奏, 别混成一个:
     *      采样: 每 APP_SAMPLE_PERIOD_MS 一次(串口每秒一行, 肉眼可见)
     *      上报: 每 APP_REPORT_PERIOD_MS 一次(与后端/前端的刷新节奏对齐)
     *    不采用"采一次报一次"是因为那样既刷串口日志, 又白白增加数据库写入。 */
    ESP_LOGI(TAG, "=== 阶段2: 采样 %d ms / 上报 %d ms, device_id=%s ===",
             APP_SAMPLE_PERIOD_MS, APP_REPORT_PERIOD_MS, APP_DEVICE_ID);

    int64_t next_report_us = esp_timer_get_time();   /* 开机立刻报一次, 页面马上就有数据 */

    while (1) {
        sample_result_t r;
        sample_once(&light_ready, &soil_ready, &salinity_ready, &temperature_ready, &r);
        log_sample(&r);

        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_report_us) {
            publish_sample(&r);
            next_report_us += (int64_t)APP_REPORT_PERIOD_MS * 1000;
            /* 万一被别的事情拖慢很久(比如 WiFi 重连阻塞), 避免一次补发好几包 */
            if (now_us > next_report_us) {
                next_report_us = now_us + (int64_t)APP_REPORT_PERIOD_MS * 1000;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_SAMPLE_PERIOD_MS));
    }
}

