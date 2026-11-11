idf_component_register(SRCS "ble_ota.c" "ota.c"
                       INCLUDE_DIRS "."
                       REQUIRES bt app_ble app_update esp_ringbuf)