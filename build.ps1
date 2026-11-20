# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds everything into .\build
#
#   build\MsiGpuForIcue.dll         the iCUE plugin
#   build\gputest.exe               hardware test without iCUE
#   build\plugintest.exe            loads the plugin like iCUE does
#
# Requires Visual Studio 2022 (the Build Tools are enough) with the C++ workload.
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'build'
$obj = Join-Path $out 'obj'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio 2022 or its Build Tools' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with C++ tools not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

$common = '/nologo /std:c++17 /EHsc /W4 /WX /permissive- /guard:cf /sdl /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX'
$ship = if ($Config -eq 'Release') { "$common /O2 /MT /DNDEBUG" } else { "$common /Od /Zi /MTd" }

function Src([string[]]$files) { ($files | ForEach-Object { "`"$root\$_`"" }) -join ' ' }

$core = Src 'src\nvapi_i2c.cpp', 'src\msi_gpu.cpp'
$plugin = Src 'src\plugin.cpp', 'src\settings.cpp', 'src\color_streamer.cpp', 'src\device_description.cpp'

function Invoke-Vc([string]$name, [string]$command) {
    $dir = Join-Path $obj $name
    New-Item -ItemType Directory -Force $dir | Out-Null
    cmd /c "`"$vcvars`" >nul 2>&1 && cd /d `"$dir`" && $command 2>&1"
    if ($LASTEXITCODE -ne 0) { throw "build failed: $name" }
}


Invoke-Vc 'plugin' ("rc /nologo /fo version.res `"$root\src\version.rc`" && " +
    "cl $ship /LD /Fe`"$out\MsiGpuForIcue.dll`" $plugin $core version.res")
Invoke-Vc 'gputest' "cl $ship /Fe`"$out\gputest.exe`" `"$root\tools\gputest.cpp`" $core"
Invoke-Vc 'plugintest' "cl $ship /Fe`"$out\plugintest.exe`" `"$root\tools\plugintest.cpp`""

# The plugin resolves the pictures relative to its own folder.
foreach ($dir in @($out)) {
    $old = Join-Path $dir 'images'
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
}
Copy-Item -Recurse -Force (Join-Path $root 'images') $out
Write-Host "Built into $out"
