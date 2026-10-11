$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$workspaceRoot=Split-Path $projectRoot -Parent
Set-Location $projectRoot
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $workspaceRoot 'toolcache\zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $workspaceRoot 'toolcache\zig-local-cache'
$compiler=Join-Path $workspaceRoot 'toolcache\zig\zig-windows-x86_64-0.13.0\zig.exe'
& $compiler cc -target wasm32-wasi -std=c17 -Wall -Wextra -Werror -I main main/ems_core.c main/expert_params.c tests/test_energy_profiles.c -o tests/test_energy_profiles.wasm
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
node tools/run_wasi_test.cjs tests/test_energy_profiles.wasm > local-tests/1.19-battery/energy-profiles.csv
$result=$LASTEXITCODE
Get-Content local-tests/1.19-battery/energy-profiles.csv -Tail 4
exit $result
