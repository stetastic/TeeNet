param([string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$workspaceRoot = Split-Path $projectRoot -Parent
Set-Location $projectRoot
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $workspaceRoot 'toolcache\zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $workspaceRoot 'toolcache\zig-local-cache'
$compiler = Join-Path $workspaceRoot 'toolcache\zig\zig-windows-x86_64-0.13.0\zig.exe'
$jsonDir = Join-Path $workspaceRoot 'toolcache\esp-idf\v5.3.2\esp-idf\components\json\cJSON'
foreach ($suite in @('heatpump','telemetry','phase_setup','assets','appearance','boot_check','http_encoding','gpio','gpio_atom','gpio_http','core','pv','battery_regression','energy_profiles','runtime','restart','review','sessions','calibration','hardware','hardware_atom','features','huawei','network','grid_fallback','fixed_phase','basic_mode','priority','support','support_atom','em24','opendtu','github_release','github_release_atom','evcc','rs485','rs485_io','bounded_socket','control_storage','expert_storage','wallbox_network','phase_status','phase_controller','phase_mqtt','phase_mqtt_storage','http_json','http_post_guard','storage_reset','session_snapshot','xemex_two','mqtt_input','phase_output_network','shelly_phase','json_memory','status_profile','config_apply','plan_http','terms','expert')) {
    if ($Only.Count -gt 0 -and $suite -notin $Only) { continue }
    $baseSuite = $suite -replace '_atom$',''
    $defines = @('-DCJSON_NESTING_LIMIT=16')
    if ($suite.EndsWith('_atom')) { $defines += '-DCONFIG_TEE_BOARD_ATOMS3_LITE=1' }
    $inputFiles = @('main/ems_core.c','main/shelly_phase.c',(Join-Path $jsonDir 'cJSON.c'))
    if ($suite -eq 'appearance') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_appearance_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $inputFiles = @((Join-Path $jsonDir 'cJSON.c'))
    }
    if ($baseSuite -eq 'support') { $inputFiles = @('main/diagnostic_store.c','main/firmware_check.c') }
    if ($suite -eq 'features' -or $suite -eq 'plan_http') { $inputFiles += @('main/ems_features.c') }
    if ($suite -eq 'huawei') { $inputFiles += @('main/huawei_modbus.c') }
    if ($suite -eq 'em24') { $inputFiles += @('main/em24_modbus.c') }
    if ($suite -eq 'opendtu') { $inputFiles += @('main/opendtu_mqtt.c') }
    if ($suite -eq 'mqtt_input') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_mqtt_input_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $inputFiles += @('main/opendtu_mqtt.c')
    }
    if ($suite -eq 'assets') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' -X utf8 tools/extract_asset_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if ($suite -eq 'config_apply') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_config_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if ($suite -eq 'phase_controller') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_phase_control_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $inputFiles += @('main/evcc_bridge.c')
    }
    if ($suite -eq 'status_profile') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_status_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $inputFiles += @('main/ems_features.c','main/evcc_bridge.c')
    }
    if ($suite -eq 'storage_reset') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_storage_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $inputFiles += @('main/charge_sessions.c')
    }
    if ($suite -eq 'phase_mqtt_storage') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' tools/extract_phase_auth_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if ($suite -eq 'shelly_phase') {
        & 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' -X utf8 tools/extract_phase_observed_test.py
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    if ($baseSuite -eq 'github_release') { $inputFiles = @('main/github_release.c',(Join-Path $jsonDir 'cJSON.c')) }
    if ($suite -eq 'terms' -or $suite -eq 'plan_http' -or $suite -eq 'phase_mqtt_storage') { $inputFiles += @((Join-Path $jsonDir 'cJSON.c')) }
    if ($suite -eq 'evcc') { $inputFiles = @('main/evcc_bridge.c',(Join-Path $jsonDir 'cJSON.c')) }
    if ($suite -eq 'session_snapshot') { $inputFiles += @('main/charge_sessions.c') }
    if ($suite -eq 'sessions') { $inputFiles = @('main/charge_sessions.c') }
    if ($suite -eq 'review') { $inputFiles += @('main/meter_json.c','main/shelly_modbus.c',(Join-Path $jsonDir 'cJSON.c')) }
    $inputFiles = @($inputFiles | Select-Object -Unique)
    $testFile = if ($suite -eq 'hardware_atom') { 'tests/test_hardware_atom.c' } else { "tests/test_$baseSuite.c" }
    $inputFiles += @("main/expert_params.c")
    $optimization = if ($suite -eq 'http_encoding') { @('-O1', '-UNDEBUG') } else { @() }
    & $compiler cc -target wasm32-wasi @optimization -std=c17 -Wall -Wextra -Werror @defines -I main -I $jsonDir @inputFiles $testFile -o "tests/test_$suite.wasm"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if ($suite -eq 'energy_profiles') {
        node tools/run_wasi_test.cjs "tests/test_$suite.wasm" > tests/energy-profiles.csv
        $profileResult = $LASTEXITCODE
        Get-Content tests/energy-profiles.csv -Tail 1
        if ($profileResult -ne 0) { exit $profileResult }
    } else {
        node tools/run_wasi_test.cjs "tests/test_$suite.wasm"
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if ($Only.Count -gt 0) { return }
node --test tests/energy.test.cjs tests/appearance-notice.test.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_evcc_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_vehicle_status.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_support_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node --check main/dashboard.js
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_control_status.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_phase_feedback_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_charge_button.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node --check main/features.js
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_web_poll.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_status_poll.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_status_profile_demo.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_settings_state.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_pin_reload.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_battery_bridge.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_energy_bridge.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_current_bridge.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_car_bridge.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_demo_grid_fallback.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_power_slider.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_em24_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_review_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_demo_sessions.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_settings_notice.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_demo_restart.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_expert_demo_restart.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_expert_edit_lock.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_expert_curve.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_expert_precision_markers.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_phase_mqtt_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_phase_mqtt_demo.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_shelly_phase_demo.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_shelly_phase_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_gpio_demo.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_phase_test_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_phase_settings_layout.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_update_notice.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

node tests/test_phase_config_draft.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_diagnostics_memory.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_charge_mode_preference.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_version_dialog.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_telemetry_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $env:TEENET_TEST_PYTHON) { $env:TEENET_TEST_PYTHON = 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' }
node tests/test_backup_zip.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

