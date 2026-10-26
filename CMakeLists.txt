cmake_minimum_required(VERSION 3.16)

# ================================================================
# ESP32 C-RID — 统一收发固件
# ================================================================
#
# 固件为单程序双模式：发射（GB42590 / GB46750）与
# 接收监测均集成在 main/ 组件中，可在网页 http://192.168.4.1
# 在线切换设备模式与广播标准。
#
# 用法:
#   idf.py set-target esp32s3
#   idf.py build
# ================================================================

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/main
                         ${CMAKE_CURRENT_SOURCE_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(remoteid_scanner)
