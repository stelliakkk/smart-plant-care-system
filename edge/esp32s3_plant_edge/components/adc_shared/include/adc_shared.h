#pragma once
#include "esp_adc/adc_oneshot.h"

/* Startup-task API. The service owns ADC1 for the application lifetime;
 * callers configure their own channels and must not delete the shared unit.
 * Unit creation is serialized; ESP-IDF guards ADC reads internally. */
esp_err_t adc_shared_get_unit(adc_unit_t unit, adc_oneshot_unit_handle_t *out);
