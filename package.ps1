# SPDX-License-Identifier: GPL-2.0-or-later
#
# Packs the release files from an existing build (run .\build.ps1 first).
#
#   .\package.ps1               dist\msi-gpu-for-icue-<version>.zip (+ .sha256)
#   .\package.ps1 -Installer    also dist\msi-gpu-for-icue-setup-<version>.exe (+ .sha256)
#
# The zip keeps the repository layout that install.ps1 expects
# (build\MsiGpuForIcue.dll next to images\ and the scripts). -Installer needs
# Inno Setup 6 (ISCC.exe on PATH, in its default folder, or passed with -Iscc).
param(
    [switch]$Installer,
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$versionLine = Select-String -Path (Join-Path $root 'src\version.h') -Pattern 'MSIGPU_VERSION_STRING "([^"]+)"'
if (-not $versionLine) { throw 'version not found in src\version.h' }
$version = $versionLine.Matches[0].Groups[1].Value
$dist = Join-Path $root 'dist'

function Write-Checksum([string]$file) {
    $hash = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
    "$hash  $(Split-Path $file -Leaf)" | Set-Content -Encoding ascii "$file.sha256"
    Write-Host "Packed $file ($hash)"
}

# --- zip --------------------------------------------------------------------
$name = "msi-gpu-for-icue-$version"
$stage = Join-Path $dist $name
Remove-Item -Recurse -Force $stage, "$stage.zip", "$stage.zip.sha256" -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $stage 'build') | Out-Null

Copy-Item (Join-Path $root 'build\MsiGpuForIcue.dll'), (Join-Path $root 'build\gputest.exe') (Join-Path $stage 'build')
Copy-Item -Recurse (Join-Path $root 'images') $stage
Copy-Item -Recurse (Join-Path $root 'docs') $stage   # the README links to it
foreach ($f in 'install.ps1', 'uninstall.ps1', 'settings.example.ini', 'README.md', 'LICENSE', 'THIRD_PARTY.md',
    'SECURITY.md', 'CHANGELOG.md') {
    Copy-Item (Join-Path $root $f) $stage
}
Compress-Archive -Path "$stage\*" -DestinationPath "$stage.zip"
Remove-Item -Recurse -Force $stage
Write-Checksum "$stage.zip"

# --- setup.exe ----------------------------------------------------------------
if ($Installer) {
    if (-not $Iscc) {
        $candidates = @((Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source,
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
            "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
        $Iscc = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    }
    if (-not $Iscc -or -not (Test-Path $Iscc)) { throw 'ISCC.exe (Inno Setup 6) not found; pass -Iscc <path>' }

    $setup = Join-Path $dist "msi-gpu-for-icue-setup-$version.exe"
    Remove-Item -Force $setup, "$setup.sha256" -ErrorAction SilentlyContinue
    & $Iscc /Q "/DAppVersion=$version" (Join-Path $root 'installer\msi-gpu-for-icue.iss')
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
    Write-Checksum $setup
}
