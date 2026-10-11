set(ems_fingerprint "")
foreach(source plan_http.inc phase_controller.inc phase_status.inc control_storage.inc expert_storage.inc expert_params.def expert_params.h expert_params.c expert_http.inc hardware_profile.h terms_http.inc rs485_transport.inc terms.js evcc_bridge.c evcc_bridge.h evcc_http.inc evcc-test.js opendtu_mqtt.c opendtu_mqtt.h opendtu_runtime.inc github_release.c github_release.h github_update.inc system_backup.inc app.c diagnostic_http.inc meter_network.inc meter_tcp.inc wallbox_network.inc diagnostic_store.c diagnostic_store.h firmware_check.c firmware_check.h diagnostic_storage.inc ota_http.inc health.js firmware.js shell_limits.inc bounded_socket.inc huawei_modbus.c huawei_modbus.h huawei_network.inc huawei_discovery.inc feature_http.inc ems_core.c ems_core.h ems_features.c ems_features.h meter_json.c meter_json.h em24_modbus.c em24_modbus.h shelly_modbus.c shelly_modbus.h charge_sessions.c charge_sessions.h dashboard.html dashboard.css dashboard.js features.js qrcode.js guide.js guide.html hardware-plan.svg teennet-logo.png wiring-plan.jpg version.json)
    file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/${source}" source_hash)
    string(APPEND ems_fingerprint "${source}:${source_hash};")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${source}")
endforeach()
if(CONFIG_TEE_BOARD_ATOMS3_LITE)
    string(APPEND ems_fingerprint "hardware-profile:andreas-testboard;")
else()
    string(APPEND ems_fingerprint "hardware-profile:esp32s3-n16r8;")
endif()
string(SHA256 ems_build_hash "${ems_fingerprint}")
string(SUBSTRING "${ems_build_hash}" 0 12 ems_build_id)
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.json" ems_release_json)
string(JSON ems_version GET "${ems_release_json}" version)
if(NOT ems_version MATCHES "^[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "version.json needs a numeric version")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ems_build.h" "#define EMS_BUILD_ID \"${ems_build_id}\"\n#define EMS_VERSION \"${ems_version}\"\n")

idf_component_register(SRCS "expert_params.c" "evcc_bridge.c" "opendtu_mqtt.c" "github_release.c" "app.c" "diagnostic_store.c" "firmware_check.c" "huawei_modbus.c" "ems_core.c" "ems_features.c" "meter_json.c" "em24_modbus.c" "shelly_modbus.c" "charge_sessions.c"
                       INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
                       REQUIRES app_update driver esp_driver_rmt esp_event esp_http_client esp_http_server esp_netif esp_timer esp_wifi mqtt nvs_flash json mbedtls spi_flash
                       EMBED_TXTFILES "terms.js" "evcc-test.js" "dashboard.html" "dashboard.css" "dashboard.js" "features.js" "health.js" "firmware.js" "qrcode.js" "teennet-logo.png" "wiring-plan.jpg" "hardware-plan.svg" "guide.html" "guide.js")

idf_build_get_property(ems_python PYTHON)
foreach(asset terms.js evcc-test.js dashboard.html dashboard.css dashboard.js features.js health.js firmware.js qrcode.js guide.js guide.html)
    set(packed "${CMAKE_CURRENT_BINARY_DIR}/${asset}.gz")
    add_custom_command(OUTPUT "${packed}"
        COMMAND "${ems_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_asset.py" "${CMAKE_CURRENT_LIST_DIR}/${asset}" "${packed}"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${asset}" "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_asset.py"
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} "${packed}" BINARY)
endforeach()
