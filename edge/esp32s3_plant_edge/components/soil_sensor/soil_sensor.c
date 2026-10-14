/*
 * =====================================================================
 *  电容式土壤湿度传感器驱动 (ADC 单次采样 + 曲线拟合校准)
 * =====================================================================
 *  目标开发板 : ESP32-S3-DevKitC-1 v1.1
 *  信号引脚   : AOUT → GPIO4 (ADC1 通道 3)
 *  供电       : ★ VCC 必须接 3.3V
 *
 *  ---------------------------------------------------------------
 *  硬件避坑指南:
 *
 *  [1] VCC 一定要接 3.3V, 绝对不能接 5V
 *      AOUT 输出电压与供电电压成正比。接 5V 时干燥状态能输出到 4V 以上,
 *      而 ESP32-S3 的 ADC 引脚耐压只有 3.3V —— 轻则读数被削顶(湿度永远
 *      显示 100%), 重则打穿引脚。电容式探头在 3.3V 下工作完全正常,
 *      没有任何理由接 5V。
 *
 *  [2] GPIO4 属于 ADC1
 *      ESP32-S3 有两组 ADC: ADC1 → GPIO1~GPIO10, ADC2 被 WiFi 驱动内部
 *      占用(开着 WiFi 去读 ADC2 会直接返回错误)。所以模拟传感器一律往
 *      ADC1 上接。本驱动用 adc_oneshot_io_to_channel() 自动把 GPIO 换算
 *      成"哪个 ADC 的第几通道", 不写死通道号 —— 换引脚只改下面一个宏。
 *
 *  [3] 衰减必须选 12dB
 *      ADC 默认量程只有约 950mV, 而探头输出 1.3~2.6V, 不换衰减会严重
 *      削顶。ADC_ATTEN_DB_12 把量程扩到约 0~3100mV, 正好覆盖。
 *
 *  [4] 为什么要连读 16 次取平均
 *      ESP32-S3 的 ADC 噪声偏大(官方文档明确说 ±几十 LSB 属正常现象),
 *      单次读数会看到末位乱跳。16 次平均能明显稳住读数, 代价只有几毫秒。
 *
 *  ---------------------------------------------------------------
 *  ⚠ 标定值(全文件最重要的一条):
 *    干/湿两个端点的电压, 决定"读到多少 mV 算多少湿度"。
 *    不同批次探头差几百 mV 很常见, 所以驱动里只留一组"能用但不准"的
 *    兜底默认值; 真正生效的值由上层通过 soil_sensor_init() 传进来。
 *    本工程把它放在 main/local_config.h —— 那里是"本机私有配置"的地盘,
 *    和 WiFi 密码同一档, 换探头不用动驱动代码。
 *
 *    想自己重标:
 *      1) 烧录后看串口, 探头悬空时记下 voltage_mv  → 填给 LOCAL_SOIL_MV_DRY
 *      2) 探头泡进一杯清水, 记下 voltage_mv        → 填给 LOCAL_SOIL_MV_WET
 *      3) 改完重新 idf.py -p COM3 flash
 *    标定前先别急着插土里 —— 否则你会以为"土很干", 其实是端点没标对。
 * =====================================================================
 */

#include "esp_check.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "soil_sensor.h"
#include "adc_shared.h"

static const char *TAG = "SOIL";

/* ------------------------------ 驱动参数 ------------------------------ */

/* 探头 AOUT 接哪个 GPIO。换接线只改这一行 */
#define SOIL_ADC_GPIO           4

/* ADC 衰减与位宽: 12dB ≈ 0~3100mV 量程, 正好覆盖探头输出范围 */
#define SOIL_ADC_ATTEN          ADC_ATTEN_DB_12
#define SOIL_ADC_BITWIDTH       ADC_BITWIDTH_DEFAULT

/* 每次采样连读多少次取平均(降噪) */
#define SOIL_AVG_TIMES          16

/* 标定端点的"兜底默认值"(mV): 干燥(悬空空气中) / 湿润(浸入水中)。
 * ⚠ 这只是业界常见值, 不代表你这支探头 —— 实际生效的值由
 *   soil_sensor_init() 的入参决定(本工程来自 main/local_config.h),
 *   这里仅在调用方传 NULL 时兜底。 */
#define SOIL_MV_DRY_DEFAULT     2600
#define SOIL_MV_WET_DEFAULT     1300

/* ------------------------------ 内部状态 ------------------------------ */

static bool s_ready = false;
static adc_oneshot_unit_handle_t s_adc = NULL;   /* ADC 单元句柄 */
static adc_cali_handle_t         s_cali = NULL;  /* 校准句柄(可能为 NULL) */
static adc_channel_t             s_chan = ADC_CHANNEL_0;  /* 运行时由引脚算出 */

/* 当前生效的标定端点, 由 soil_sensor_init() 写入 */
static int s_mv_dry = SOIL_MV_DRY_DEFAULT;
static int s_mv_wet = SOIL_MV_WET_DEFAULT;

/* ------------------------------ 内部工具 ------------------------------ */

/**
 * @brief 把电压换算成湿度百分比(线性插值 + 钳位)
 *
 * 干端点 → 0%, 湿端点 → 100%, 中间线性过渡。
 * 超出标定范围时钳到 0~100, 免得界面上冒出 -15% 或 137% 这种数字。
 */
static float soil_mv_to_percent(int mv)
{
    const float span = (float)(s_mv_dry - s_mv_wet);
    float pct = (float)(s_mv_dry - mv) * 100.0f / span;

    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

/* ------------------------------ 驱动实现 ------------------------------ */

esp_err_t soil_sensor_init(const soil_calibration_t *cal)
{
    if (s_ready) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    /* 0) 先确定标定端点: 上层传了就用, 没传就用兜底默认值。
     *    干湿端点填反会让湿度永远显示 0% 或 100%, 所以这里拦住。 */
    if (cal != NULL) {
        ESP_RETURN_ON_FALSE(cal->mv_dry > cal->mv_wet, ESP_ERR_INVALID_ARG, TAG,
                            "标定端点不合法: 干燥(%d) 必须大于 湿润(%d) mV",
                            cal->mv_dry, cal->mv_wet);
        s_mv_dry = cal->mv_dry;
        s_mv_wet = cal->mv_wet;
    } else {
        s_mv_dry = SOIL_MV_DRY_DEFAULT;
        s_mv_wet = SOIL_MV_WET_DEFAULT;
        ESP_LOGW(TAG, "调用方未提供标定端点, 使用兜底默认值(仅保证能用, 不代表准确)");
    }

    /* 1) 引脚 → ADC 单元/通道。写死通道号在换引脚时容易留下隐患, 交给驱动算 */
    adc_unit_t unit = ADC_UNIT_1;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(SOIL_ADC_GPIO, &unit, &s_chan), TAG,
                        "GPIO%d 不是可用的 ADC 引脚", SOIL_ADC_GPIO);
    ESP_LOGI(TAG, "土壤探头: GPIO%d = ADC%d 通道 %d",
             SOIL_ADC_GPIO, (int)unit + 1, (int)s_chan);

    /* 2) 建 ADC 单元 */
    ESP_RETURN_ON_ERROR(adc_shared_get_unit(unit, &s_adc), TAG, "get shared ADC1 failed");

    /* 3) 按选定量程/位宽配置这个通道 */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = SOIL_ADC_ATTEN,
        .bitwidth = SOIL_ADC_BITWIDTH,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_chan, &chan_cfg), TAG,
                        "config ADC channel failed");

    /* 4) 曲线拟合校准: 把原始值换算成真实电压(mV)。
     *    没有它只能按线性关系粗估, 误差能到几十毫伏 —— 放在 1.3V 宽的
     *    湿度区间里, 那就是好几个百分点的偏差。 */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_chan,
        .atten = SOIL_ADC_ATTEN,
        .bitwidth = SOIL_ADC_BITWIDTH,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "曲线拟合校准不可用(%s), 降级为线性估算电压", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "ADC 曲线拟合校准已启用");
    }

    s_ready = true;
    ESP_LOGI(TAG, "土壤探头就绪 (生效标定端点: 干燥 %d mV / 湿润 %d mV; 悬空应≈0%%, 泡水应≈100%%)",
             s_mv_dry, s_mv_wet);
    return ESP_OK;
}

esp_err_t soil_sensor_read(soil_reading_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is NULL");
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call soil_sensor_init() first");

    /* 连读 16 次取平均: ADC 本身噪声不小, 平均之后读数才不会末位乱跳 */
    int sum = 0;
    for (int i = 0; i < SOIL_AVG_TIMES; i++) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, s_chan, &raw), TAG, "ADC read failed");
        sum += raw;
    }
    const int raw_avg = sum / SOIL_AVG_TIMES;

    /* 原始值 → 电压(mV) */
    int mv = 0;
    if (s_cali != NULL) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw_avg, &mv), TAG,
                            "raw to voltage failed");
    } else {
        /* 没有校准时的退路: 按 12dB 满量程 3100mV 线性估算 */
        mv = raw_avg * 3100 / 4095;
    }

    out->raw = raw_avg;
    out->voltage_mv = mv;
    out->percent = soil_mv_to_percent(mv);
    return ESP_OK;
}

