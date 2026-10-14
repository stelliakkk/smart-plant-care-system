idf_component_register(SRCS "mqtt_report.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES mqtt esp_timer wifi_sta plant_control)
