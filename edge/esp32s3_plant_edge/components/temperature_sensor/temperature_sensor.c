/*
 * ESP32-S3 DS18B20 温度传感器驱动
 * ---------------------------------------------------------------------
 * 数据脚: GPIO8 (1-Wire，不是 ADC)
 * 默认使用 12 位分辨率。
 *
 * GPIO8 建议外接 4.7kΩ 上拉到 3.3V；内部上拉只作为没有外部电阻时的
 * 兜底，实际长线和较稳定的温度读取仍应使用外部上拉。
 */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#include "esp_check.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"

#include "temperature_sensor.h"

static const char *TAG = "TEMPERATURE";

#define TEMPERATURE_ONEWIRE_GPIO  8

static onewire_bus_handle_t s_bus = NULL;
static ds18b20_device_handle_t s_device = NULL;
static bool s_initialized = false;

esp_err_t temperature_sensor_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "already initialized, skip");
        return ESP_OK;
    }

    if (s_bus != NULL) {
        ESP_LOGW(TAG, "1-Wire bus already exists but no DS18B20 is ready");
        return ESP_ERR_NOT_FOUND;
    }

    const onewire_bus_config_t bus_config = {
        .bus_gpio_num = TEMPERATURE_ONEWIRE_GPIO,
        .flags = {
            .en_pull_up = true,
        },
    };
    const onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };

    ESP_RETURN_ON_ERROR(onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus), TAG,
                        "create 1-Wire bus failed");
    ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d", TEMPERATURE_ONEWIRE_GPIO);

    onewire_device_iter_handle_t iter = NULL;
    ESP_RETURN_ON_ERROR(onewire_new_device_iter(s_bus, &iter), TAG,
                        "create 1-Wire device iterator failed");

    onewire_device_t next_device;
    esp_err_t search_result = ESP_OK;
    do {
        search_result = onewire_device_iter_get_next(iter, &next_device);
        if (search_result == ESP_OK) {
            ds18b20_config_t ds_config = { };
            if (ds18b20_new_device_from_enumeration(&next_device, &ds_config, &s_device) == ESP_OK) {
                onewire_device_address_t address = 0;
                ds18b20_get_device_address(s_device, &address);
                ESP_LOGI(TAG, "Found DS18B20 on GPIO%d, address=%016llX",
                         TEMPERATURE_ONEWIRE_GPIO, (unsigned long long)address);
                break;
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND);

    ESP_RETURN_ON_ERROR(onewire_del_device_iter(iter), TAG,
                        "delete 1-Wire device iterator failed");

    if (s_device == NULL) {
        ESP_LOGW(TAG, "No DS18B20 found on GPIO%d; check VCC, GND, DATA and 4.7k pull-up",
                 TEMPERATURE_ONEWIRE_GPIO);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(ds18b20_set_resolution(s_device, DS18B20_RESOLUTION_12B), TAG,
                        "set DS18B20 resolution failed");

    s_initialized = true;
    ESP_LOGI(TAG, "DS18B20 temperature sensor ready (12-bit)");
    return ESP_OK;
}

esp_err_t temperature_sensor_read(float *temperature_c)
{
    ESP_RETURN_ON_FALSE(temperature_c != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "temperature_c is NULL");
    ESP_RETURN_ON_FALSE(s_initialized && s_bus != NULL && s_device != NULL,
                        ESP_ERR_INVALID_STATE, TAG,
                        "not initialized, call temperature_sensor_init() first");

    ESP_RETURN_ON_ERROR(ds18b20_trigger_temperature_conversion_for_all(s_bus), TAG,
                        "trigger DS18B20 conversion failed");
    return ds18b20_get_temperature(s_device, temperature_c);
}
