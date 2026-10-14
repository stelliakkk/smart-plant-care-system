idf_component_register(SRCS "plant_edge.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES actuator_pump saltwater_pump spray_pump grow_light
                                     rgb_led board_info esp_timer i2c_bus light_sensor soil_sensor salinity_sensor
                                     temperature_sensor
                                     wifi_sta mqtt_report plant_control)

