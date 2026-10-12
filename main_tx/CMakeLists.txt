idf_component_register(
    SRCS
        crid-sim.c
        crid_config.c
        crid_messages.c
        crid_wifi.c
        crid_patrol.c
        crid_web.c
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        nvs_flash
        esp_common
        esp_netif
        esp_event
        esp_http_server
        cjson
    PRIV_REQUIRES soc)

target_link_libraries(${COMPONENT_LIB} m)
