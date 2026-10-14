/*
 * ESP32-S3 土壤盐分传感器驱动
 * ---------------------------------------------------------------------
 * 信号脚: GPIO7 (ADC1)
 * 输出: ADC raw + 校准后的 mV
 *
 * GPIO7 是 ADC1 引脚，可以在 WiFi 工作时正常使用。传感器模拟输出
 * 不得超过 3.3V；具体 EC/盐分换算留给后续标定配置。
 */
#include "esp_check.h"
#include "esp_log.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#include "salinity_sensor.h"
#include "adc_shared.h"

static const char *TAG = "SALINITY";

/* -------------------------- 可调驱动参数 -------------------------- */
#define SALINITY_ADC_GPIO       7
#define SALINITY_ADC_ATTEN      ADC_ATTEN_DB_12
#define SALINITY_ADC_BITWIDTH   ADC_BITWIDTH_DEFAULT
#define SALINITY_AVG_TIMES      16

static bool s_ready = false;
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static adc_channel_t s_channel = ADC_CHANNEL_0;

esp_err_t salinity_sensor_init(void)
{
    if (s_ready) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    adc_unit_t unit = ADC_UNIT_1;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(SALINITY_ADC_GPIO, &unit, &s_channel),
                        TAG, "GPIO%d 不是可用的 ADC 引脚", SALINITY_ADC_GPIO);

    ESP_LOGI(TAG, "Init salinity ADC: GPIO%d = ADC%d channel %d",
             SALINITY_ADC_GPIO, (int)unit + 1, (int)s_channel);

    ESP_RETURN_ON_ERROR(adc_shared_get_unit(unit, &s_adc), TAG, "get shared ADC1 failed");

    const adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = SALINITY_ADC_ATTEN,
        .bitwidth = SALINITY_ADC_BITWIDTH,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_channel, &channel_cfg), TAG,
                        "configure salinity ADC channel failed");

    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = SALINITY_ADC_ATTEN,
        .bitwidth = SALINITY_ADC_BITWIDTH,
    };
    const esp_err_t cali_err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (cali_err != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "ADC calibration unavailable (%s), use linear estimate",
                 esp_err_to_name(cali_err));
    } else {
        ESP_LOGI(TAG, "ADC curve-fitting calibration enabled");
    }

    s_ready = true;
    ESP_LOGI(TAG, "Salinity sensor ready: GPIO%d, ADC1", SALINITY_ADC_GPIO);
    return ESP_OK;
}

esp_err_t salinity_sensor_read(salinity_reading_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out is NULL");
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call salinity_sensor_init() first");

    int sum = 0;
    for (int i = 0; i < SALINITY_AVG_TIMES; ++i) {
        int raw = 0;
        ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc, s_channel, &raw), TAG,
                            "salinity ADC read failed");
        sum += raw;
    }

    const int raw_average = sum / SALINITY_AVG_TIMES;
    int voltage_mv = 0;
    if (s_cali != NULL) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_cali, raw_average, &voltage_mv), TAG,
                            "salinity raw to voltage failed");
    } else {
        voltage_mv = raw_average * 3100 / 4095;
    }

    out->raw = raw_average;
    out->voltage_mv = voltage_mv;
    return ESP_OK;
}

