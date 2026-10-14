idf_component_register(SRCS "plant_control.c" INCLUDE_DIRS "include"
    REQUIRES espressif__cjson PRIV_REQUIRES actuator_pump saltwater_pump spray_pump grow_light nvs_flash esp_timer esp_hw_support wifi_sta)
