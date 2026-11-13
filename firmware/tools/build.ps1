param(
    [ValidateSet('esp32s3_n16r8','atoms3_lite_8mb')]
    [string]$Target = 'esp32s3_n16r8',
    [string]$BuildDirectory = '',
    [switch]$HttpControlTrial
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$workspaceRoot = Split-Path $projectRoot -Parent
$env:IDF_PATH = Join-Path $workspaceRoot 'toolcache\esp-idf\v5.3.2\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\esp-tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\esp-tools\python_env\idf5.3_py3.12_env'
$env:Path = 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts;C:\esp-tools\tools\cmake\3.30.2\bin;C:\esp-tools\tools\ninja\1.12.1;C:\esp-tools\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;' + $env:Path
Set-Location $projectRoot
if (-not $BuildDirectory) {
    $BuildDirectory = if ($Target -eq 'esp32s3_n16r8') { 'build' } else { 'build-atoms3-lite' }
}
$archiveTool = 'C:/esp-tools/tools/xtensa-esp-elf/esp-13.2.0_20240530/xtensa-esp-elf/bin/xtensa-esp-elf-ar.exe'
$trialOption = if ($HttpControlTrial) { '-DTEE_HTTP_CONTROL_TRIAL=ON' } else { '-DTEE_HTTP_CONTROL_TRIAL=OFF' }
# SDKCONFIG_DEFAULTS only initializes new build directories. Explicitly clear
# a previous private selection when reusing a directory for a public build.
$trialConfigPath = Join-Path (Join-Path $projectRoot $BuildDirectory) 'sdkconfig'
if (Test-Path -LiteralPath $trialConfigPath) {
    $trialConfigText = [System.IO.File]::ReadAllText($trialConfigPath)
    $trialConfigLine = if ($HttpControlTrial) { 'CONFIG_TEE_HTTP_CONTROL_TRIAL=y' } else { '# CONFIG_TEE_HTTP_CONTROL_TRIAL is not set' }
    $trialConfigPattern = '(?m)^(?:# )?CONFIG_TEE_HTTP_CONTROL_TRIAL(?:=y| is not set)\r?$'
    if ([regex]::IsMatch($trialConfigText, $trialConfigPattern)) {
        $trialConfigText = [regex]::Replace($trialConfigText, $trialConfigPattern, $trialConfigLine)
    } else {
        $trialConfigText += "`n$trialConfigLine`n"
    }
    [System.IO.File]::WriteAllText($trialConfigPath, $trialConfigText, [System.Text.UTF8Encoding]::new($false))
}
# GNU ar -s creates the same archive index as ranlib, without its launcher.
& 'C:\esp-tools\python_env\idf5.3_py3.12_env\Scripts\python.exe' (Join-Path $env:IDF_PATH 'tools\idf.py') -B $BuildDirectory "-DBUILD_TARGET=$Target" $trialOption "-DCMAKE_AR=$archiveTool" '-DCMAKE_C_ARCHIVE_FINISH=<CMAKE_AR> -s <TARGET>' '-DCMAKE_CXX_ARCHIVE_FINISH=<CMAKE_AR> -s <TARGET>' build
exit $LASTEXITCODE
