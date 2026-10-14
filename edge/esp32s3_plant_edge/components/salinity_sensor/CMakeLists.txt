idf_component_register(SRCS "salinity_sensor.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_adc PRIV_REQUIRES adc_shared)

