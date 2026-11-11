idf_component_register(SRCS "app_ble.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nimble_peripheral_utils
                       REQUIRES bt)