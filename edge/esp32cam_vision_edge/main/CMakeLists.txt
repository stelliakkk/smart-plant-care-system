idf_component_register(SRCS "esp32cam_vision.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_event esp_netif nvs_flash
                                esp_http_server esp_timer espressif__mqtt
                                espressif__esp32-camera)
