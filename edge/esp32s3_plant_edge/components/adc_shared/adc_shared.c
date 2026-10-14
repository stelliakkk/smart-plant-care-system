#include "adc_shared.h"
#include <sys/lock.h>

static adc_oneshot_unit_handle_t adc1;
static _lock_t init_lock;

esp_err_t adc_shared_get_unit(adc_unit_t unit, adc_oneshot_unit_handle_t *out)
{
    if (!out || unit != ADC_UNIT_1) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    _lock_acquire(&init_lock);
    if (!adc1) {
        const adc_oneshot_unit_init_cfg_t cfg = {.unit_id = ADC_UNIT_1};
        adc_oneshot_unit_handle_t handle = NULL;
        esp_err_t err = adc_oneshot_new_unit(&cfg, &handle);
        if (err != ESP_OK) {
            _lock_release(&init_lock);
            return err;
        }
        adc1 = handle;
    }
    *out = adc1;
    _lock_release(&init_lock);
    return ESP_OK;
}
