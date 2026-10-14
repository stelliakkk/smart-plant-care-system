#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"
typedef void (*plant_control_ack_fn)(const char *json);
esp_err_t plant_control_init(plant_control_ack_fn callback);
void plant_control_connection(bool connected);
void plant_control_receive(const char *json, size_t length, bool retained);
void plant_control_status(cJSON *root);

void plant_control_light_sample(float lux);
