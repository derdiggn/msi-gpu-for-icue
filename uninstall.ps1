# SPDX-License-Identifier: GPL-2.0-or-later
#
# Removes msi-gpu-for-icue from iCUE. Run from an elevated Windows PowerShell:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\uninstall.ps1 [-RemoveData] [-KeepCertificate]
#
#   -RemoveData       also removes %ProgramData%\MsiGpuForIcue (settings.ini)
#   -KeepCertificate  leaves the trusted public certificate in place
#
# The trusted certificates are always removed, even if an earlier step fails
# (for example because iCUE does not exit). The log in
# %LOCALAPPDATA%\MsiGpuForIcue belongs to the user and is left alone.
#Requires -Version 5.1
#Requires -PSEdition Desktop
param(
    [string]$IcueDir = ([Environment]::GetFolderPath('ProgramFiles') + '\Corsair\Corsair iCUE5 Software'),
    [switch]$RemoveData,
    [switch]$KeepCertificate,
    [string]$LogFile = ''
)

# --- harden the elevated session (see install.ps1) -----------------------------
$system32 = [Environment]::GetFolderPath('System')
$moduleRoot = "$system32\WindowsPowerShell\v1.0\Modules"
$env:PSModulePath = $moduleRoot
$PSModuleAutoLoadingPreference = 'None'
foreach ($module in 'Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Security',
    'Microsoft.PowerShell.Host') {
    Import-Module "$moduleRoot\$module\$module.psd1" -ErrorAction Stop
}
$ErrorActionPreference = 'Stop'

if ($LogFile) { Start-Transcript -LiteralPath $LogFile -Force | Out-Null }
try {

$subject = 'CN=msi-gpu-for-icue local plugin signing'
$trustedOwners = 'S-1-5-32-544', 'S-1-5-18'   # Administrators, SYSTEM

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Please start PowerShell with "Run as administrator" and run this script again.'
}

function Test-ReparsePoint([string]$path) {
    (Test-Path -LiteralPath $path) -and
        ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}

try {
    # --- plugin files ---------------------------------------------------------
    Get-Process -Name 'iCUE' -ErrorAction SilentlyContinue | Stop-Process -Force
    $hostSvc = Get-Service -Name 'iCUEDevicePluginHost' -ErrorAction SilentlyContinue
    if ($hostSvc -and $hostSvc.Status -ne 'Stopped') { Stop-Service -Name 'iCUEDevicePluginHost' -Force }
    Get-Process -Name 'iCUEDevicePluginHost' -ErrorAction SilentlyContinue | Stop-Process -Force
    $deadline = (Get-Date).AddSeconds(15)
    while (Get-Process -Name 'iCUE', 'iCUEDevicePluginHost' -ErrorAction SilentlyContinue) {
        if ((Get-Date) -gt $deadline) { throw 'iCUE did not exit; close it (tray icon > Quit) and run this script again.' }
        Start-Sleep -Milliseconds 250
    }

    $IcueDir = [IO.Path]::GetFullPath($IcueDir).TrimEnd('\')
    $plugins = Join-Path $IcueDir 'plugins'
    $target = Join-Path $plugins 'MsiGpuForIcue'
    foreach ($path in $IcueDir, $plugins, $target) {
        if (Test-ReparsePoint $path) { throw "$path is a link; refusing to delete through it. Remove the plugin manually." }
    }
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
        Write-Host "Removed: $target"
    }

    # --- settings -------------------------------------------------------------
    if ($RemoveData) {
        $data = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'MsiGpuForIcue'
        if (Test-Path -LiteralPath $data) {
            # Only delete what the installer created: an administrator-owned
            # folder without links anywhere inside (standard users may create
            # folders in ProgramData).
            $items = @(Get-Item -LiteralPath $data -Force) + @(Get-ChildItem -LiteralPath $data -Recurse -Force)
            foreach ($item in $items) {
                $owner = (Get-Acl -LiteralPath $item.FullName).GetOwner([Security.Principal.SecurityIdentifier]).Value
                if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $owner -notin $trustedOwners) {
                    throw "$($item.FullName) was not created by the installer (owner $owner). Remove $data manually."
                }
            }
            Remove-Item -LiteralPath $data -Recurse -Force
            Write-Host "Removed: $data"
        }
    }
} finally {
    # --- certificates: always, even if the steps above failed -----------------
    if (-not $KeepCertificate) {
        foreach ($store in 'My', 'Root', 'TrustedPublisher') {
            Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $subject | ForEach-Object {
                if ($store -eq 'My') { Remove-Item -LiteralPath $_.PSPath -DeleteKey } else { Remove-Item -LiteralPath $_.PSPath }
                Write-Host "Removed certificate $($_.Thumbprint) from LocalMachine\$store"
            }
        }
    }
}

} catch {
    Write-Host "ERROR: $($_.Exception.Message)"
    throw
} finally {
    if ($LogFile) { Stop-Transcript | Out-Null }
}
