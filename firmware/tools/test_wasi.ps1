param([string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$workspaceRoot = Split-Path $projectRoot -Parent
Set-Location $projectRoot
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $workspaceRoot 'toolcache\zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $workspaceRoot 'toolcache\zig-local-cache'
$compiler = Join-Path $workspaceRoot 'toolcache\zig\zig-windows-x86_64-0.13.0\zig.exe'
$jsonDir = Join-Path $workspaceRoot 'toolcache\esp-idf\v5.3.2\esp-idf\components\json\cJSON'
foreach ($suite in @('core','pv','battery_regression','energy_profiles','runtime','restart','review','sessions','calibration','hardware','hardware_atom','features','huawei','network','grid_fallback','fixed_phase','basic_mode','priority','support','support_atom','em24','opendtu','github_release','github_release_atom','evcc','rs485','rs485_io','bounded_socket','control_storage','expert_storage','wallbox_network','phase_status','phase_controller','plan_http','terms','expert')) {
    if ($Only.Count -gt 0 -and $suite -notin $Only) { continue }
    $baseSuite = $suite -replace '_atom$',''
    $defines = @()
    if ($suite.EndsWith('_atom')) { $defines += '-DCONFIG_TEE_BOARD_ATOMS3_LITE=1' }
    $inputFiles = @('main/ems_core.c')
    if ($baseSuite -eq 'support') { $inputFiles = @('main/diagnostic_store.c','main/firmware_check.c') }
    if ($suite -eq 'features' -or $suite -eq 'plan_http') { $inputFiles += @('main/ems_features.c') }
    if ($suite -eq 'huawei') { $inputFiles += @('main/huawei_modbus.c') }
    if ($suite -eq 'em24') { $inputFiles += @('main/em24_modbus.c') }
    if ($suite -eq 'opendtu') { $inputFiles += @('main/opendtu_mqtt.c') }
    if ($baseSuite -eq 'github_release') { $inputFiles = @('main/github_release.c',(Join-Path $jsonDir 'cJSON.c')) }
    if ($suite -eq 'terms' -or $suite -eq 'plan_http') { $inputFiles += @((Join-Path $jsonDir 'cJSON.c')) }
    if ($suite -eq 'evcc') { $inputFiles = @('main/evcc_bridge.c',(Join-Path $jsonDir 'cJSON.c')) }
    if ($suite -eq 'sessions') { $inputFiles = @('main/charge_sessions.c') }
    if ($suite -eq 'review') { $inputFiles += @('main/meter_json.c','main/shelly_modbus.c',(Join-Path $jsonDir 'cJSON.c')) }
    $testFile = if ($suite -eq 'hardware_atom') { 'tests/test_hardware_atom.c' } else { "tests/test_$baseSuite.c" }
    $inputFiles += @("main/expert_params.c")
    & $compiler cc -target wasm32-wasi -std=c17 -Wall -Wextra -Werror @defines -I main -I $jsonDir @inputFiles $testFile -o "tests/test_$suite.wasm"
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
node tests/test_evcc_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_support_ui.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node --check main/dashboard.js
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_control_status.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_charge_button.cjs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node --check main/features.js
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
node tests/test_web_poll.cjs
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
