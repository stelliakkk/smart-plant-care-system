idf_component_register(SRCS "saltwater_pump.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_ledc freertos)
