idf_component_register(SRCS "temperature_sensor.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ds18b20 onewire_bus)
