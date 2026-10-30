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

# ================================================================
# 内置网页 gzip 头文件：由 crid_web.c 的 PAGE_HTML 数组生成
#
# 这里必须由 CMake 管依赖，不能只靠 build.ps1 调用 gen_gzpage.py。
# 之前踩过的坑：直接 `idf.py -B build_c5 build` 不走 build.ps1，
# page_gz.h 就一直停留在旧版本，而浏览器带 Accept-Encoding: gzip，
# 拿到的正是这份旧页面——源码已经改好、固件也重新编了，网页却一点没变。
# 表现是「改了页面功能但设备上完全看不到」，极难从日志上发现。
# ================================================================
set(PAGE_GZ_H "${CMAKE_CURRENT_SOURCE_DIR}/page_gz.h")
set(GEN_PAGE_PY "${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_gzpage.py")
# 用 IDF 自己解析出的解释器（-DPYTHON= 传入的那个），不依赖 PATH 里的 python
idf_build_get_property(idf_python PYTHON)

add_custom_command(
    OUTPUT "${PAGE_GZ_H}"
    COMMAND "${idf_python}" "${GEN_PAGE_PY}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/crid_web.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/rxmap_page.js"
            "${GEN_PAGE_PY}"
    COMMENT "从 crid_web.c 生成内置网页 gzip 头文件 page_gz.h"
    VERBATIM)

add_custom_target(gen_page_gz DEPENDS "${PAGE_GZ_H}")
add_dependencies(${COMPONENT_LIB} gen_page_gz)
# 双保险：确保 crid_web.c.obj 一定在 page_gz.h 生成之后才编译
set_source_files_properties(crid_web.c PROPERTIES OBJECT_DEPENDS "${PAGE_GZ_H}")
