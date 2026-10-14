idf_component_register(SRCS "actuator_pump.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_ledc freertos)
