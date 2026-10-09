cmake_minimum_required(VERSION 3.16)

# 1. Liest die release.json aus dem Hauptordner (eine Ebene über dem firmware-Ordner) aus
file(READ "${CMAKE_CURRENT_LIST_DIR}/../release.json" ems_release_json)
string(JSON ems_version GET "${ems_release_json}" version)
set(PROJECT_VER "${ems_version}")

# 2. Automatisierte Build-Target-Weiche für 8MB / 16MB
if(NOT DEFINED BUILD_TARGET)
    set(BUILD_TARGET "esp32_16mb")
endif()

message(STATUS ">> Kompiliere Wallbox-EMS Target: ${BUILD_TARGET}")

# SDKCONFIG_DEFAULTS passend zum Target setzen
if(${BUILD_TARGET} STREQUAL "atoms3_lite_8mb")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.atoms3_8mb")
else()
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.16mb")
endif()

# 3. ESP-IDF Projektaufrufe (beibehalten)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wallbox_ems)
