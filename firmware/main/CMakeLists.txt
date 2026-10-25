set(ems_fingerprint "")
foreach(source heatpump_runtime.inc phase_config_guard.inc phase_test_runtime.inc phase_test_http.inc boot_check.h boot_check.inc http_encoding.h gpio_map.h gpio_map.inc gpio_http.inc http_json.inc http_post_guard.inc json_owned.h session_snapshot.inc shell_search_schedule.h shelly_phase.h shelly_phase.c phase_shelly_runtime.inc phase_shelly_network.inc phase_output_network.inc phase_mqtt_network.inc phase_mqtt_runtime.inc phase_mqtt_storage.inc phase_mqtt_http.inc plan_http.inc phase_controller.inc phase_status.inc control_storage.inc expert_storage.inc expert_params.def expert_params.h expert_params.c expert_http.inc expert-schema.json hardware_profile.h terms_http.inc telemetry_core.h telemetry_http.inc telemetry_endpoint.h rs485_transport.inc terms.js evcc_bridge.c evcc_bridge.h evcc_http.inc evcc-test.js opendtu_mqtt.c opendtu_mqtt.h opendtu_runtime.inc github_release.c github_release.h github_update.inc system_backup.inc app.c diagnostic_http.inc meter_network.inc meter_tcp.inc wallbox_network.inc diagnostic_store.c diagnostic_store.h firmware_check.c firmware_check.h diagnostic_storage.inc ota_http.inc health.js firmware.js shell_limits.inc bounded_socket.inc huawei_modbus.c huawei_modbus.h huawei_network.inc huawei_discovery.inc feature_http.inc ems_core.c ems_core.h ems_features.c ems_features.h meter_json.c meter_json.h em24_modbus.c em24_modbus.h shelly_modbus.c shelly_modbus.h charge_sessions.c charge_sessions.h dashboard.html dashboard.css energy.js dashboard.js features.js qrcode.js guide.js guide.html hardware-plan.svg teennet-logo.png wiring-plan.jpg version.json)
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

idf_component_register(SRCS "shelly_phase.c" "expert_params.c" "evcc_bridge.c" "opendtu_mqtt.c" "github_release.c" "app.c" "diagnostic_store.c" "firmware_check.c" "huawei_modbus.c" "ems_core.c" "ems_features.c" "meter_json.c" "em24_modbus.c" "shelly_modbus.c" "charge_sessions.c"
                       INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
                       REQUIRES app_update driver esp_driver_rmt esp_event esp_http_client esp_http_server esp_netif esp_timer esp_wifi mqtt nvs_flash json mbedtls spi_flash
                       EMBED_TXTFILES "teennet-logo.png" "wiring-plan.jpg" "hardware-plan.svg")

idf_build_get_property(ems_python PYTHON)
# Network JSON must not exhaust a small ESP task stack through deep nesting.
idf_component_get_property(ems_json_lib json COMPONENT_LIB)
target_compile_definitions(${ems_json_lib} PRIVATE CJSON_NESTING_LIMIT=16)
if(NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    message(FATAL_ERROR "OTA self-test requires a rollback-enabled bootloader build")
endif()
if(NOT CONFIG_ESP_HTTP_CLIENT_ENABLE_DIGEST_AUTH)
    message(FATAL_ERROR "Shelly password support requires CONFIG_ESP_HTTP_CLIENT_ENABLE_DIGEST_AUTH=y")
endif()
foreach(asset terms.js evcc-test.js dashboard.html dashboard.css energy.js dashboard.js features.js health.js firmware.js qrcode.js guide.js guide.html expert-schema.json)
    set(packed "${CMAKE_CURRENT_BINARY_DIR}/${asset}.gz")
    add_custom_command(OUTPUT "${packed}"
        COMMAND "${ems_python}" "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_asset.py" "${CMAKE_CURRENT_LIST_DIR}/${asset}" "${packed}"
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/${asset}" "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_asset.py"
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} "${packed}" BINARY)
endforeach()
