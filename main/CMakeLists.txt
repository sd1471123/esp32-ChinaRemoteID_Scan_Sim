idf_component_register(
    SRCS
        crid-sim.c
        crid_config.c
        crid_messages.c
        crid_gb46750.c
        crid_time.c
        crid_wifi.c
        crid_patrol.c
        crid_web.c
        crid_rx.c
        crid_ota.c
        crid_log.c
        crid_ble.c
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        nvs_flash
        esp_common
        esp_netif
        esp_event
        esp_http_server
        esp_https_server
        esp_http_client
        esp_timer
        bt
        cjson
        app_update
        esp_app_format
        esp_partition
    # 自签证书直接嵌进固件：_binary_servercert_pem_start / _binary_prvtkey_pem_start
    EMBED_TXTFILES "certs/servercert.pem" "certs/prvtkey.pem"
    PRIV_REQUIRES soc opendroneid)

target_link_libraries(${COMPONENT_LIB} m)
