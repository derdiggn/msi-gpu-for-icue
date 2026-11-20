# SPDX-License-Identifier: GPL-2.0-or-later
#
# Installs msi-gpu-for-icue into iCUE 5. Run from an elevated Windows PowerShell:
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
#
# (The setup.exe runs this script the same way from C:\Program Files\msi-gpu-for-icue.)
#
# What it does:
#   1. prepares %ProgramData%\MsiGpuForIcue (settings.ini), writable by administrators only
#   2. creates a one-time code-signing certificate "msi-gpu-for-icue local plugin signing",
#      signs the DLL in an administrator-only staging folder and deletes the private key
#      right away; only the public part stays trusted (LocalMachine\Root + TrustedPublisher),
#      because iCUE only loads plugins whose Authenticode signature Windows trusts
#   3. installs the DLL and the images to <iCUE>\plugins\MsiGpuForIcue and removes the
#      certificates of earlier installations
#
# The elevated session trusts nothing the non-elevated user controls: modules,
# tools and system folders are resolved from Windows itself, never from
# PSModulePath, PATH or environment variables, and nothing is staged in a
# user-writable location.
#Requires -Version 5.1
#Requires -PSEdition Desktop
param(
    [string]$IcueDir = ([Environment]::GetFolderPath('ProgramFiles') + '\Corsair\Corsair iCUE5 Software'),
    [string]$LogFile = ''
)

# --- harden the elevated session ----------------------------------------------
$system32 = [Environment]::GetFolderPath('System')
$moduleRoot = "$system32\WindowsPowerShell\v1.0\Modules"
$env:PSModulePath = $moduleRoot
$PSModuleAutoLoadingPreference = 'None'
foreach ($module in 'Microsoft.PowerShell.Management', 'Microsoft.PowerShell.Utility', 'Microsoft.PowerShell.Security',
    'Microsoft.PowerShell.Host', 'PKI') {
    Import-Module "$moduleRoot\$module\$module.psd1" -ErrorAction Stop
}
$ErrorActionPreference = 'Stop'
$icacls = "$system32\icacls.exe"

if ($LogFile) { Start-Transcript -LiteralPath $LogFile -Force | Out-Null }
try {

$root = $PSScriptRoot
$subject = 'CN=msi-gpu-for-icue local plugin signing'
$trustedOwners = 'S-1-5-32-544', 'S-1-5-18'   # Administrators, SYSTEM
$programData = [Environment]::GetFolderPath('CommonApplicationData')

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Please start PowerShell with "Run as administrator" and run this script again.'
}

$IcueDir = [IO.Path]::GetFullPath($IcueDir).TrimEnd('\')
$dll = Join-Path $root 'build\MsiGpuForIcue.dll'
if (-not (Test-Path -LiteralPath $dll)) { throw "Not found: $dll  (run .\build.ps1 first)" }
if (-not (Test-Path -LiteralPath (Join-Path $IcueDir 'iCUE.exe'))) {
    throw "iCUE 5 not found in $IcueDir (use -IcueDir to point to it)"
}

function Test-ReparsePoint([string]$path) {
    (Test-Path -LiteralPath $path) -and
        ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)
}

function Get-OwnerSid([string]$path) {
    (Get-Acl -LiteralPath $path).GetOwner([Security.Principal.SecurityIdentifier]).Value
}

# Folders whose ACL lets anyone but administrators, SYSTEM and TrustedInstaller write.
function Test-WritableByOthers([string]$path) {
    $admins = 'S-1-5-18', 'S-1-5-32-544', 'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464'
    # 0x50000000 = GENERIC_ALL | GENERIC_WRITE, which ACLs may hold unmapped.
    $write = [int][Security.AccessControl.FileSystemRights]'WriteData, AppendData, Delete, ChangePermissions, TakeOwnership' -bor 0x50000000
    # Read the rules as SIDs: translating names back fails for some built-in
    # principals such as ALL APPLICATION PACKAGES, which every Program Files ACL has.
    $rules = (Get-Acl -LiteralPath $path).GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])
    foreach ($rule in $rules) {
        if ($rule.AccessControlType -ne 'Allow' -or ($rule.PropagationFlags -band [Security.AccessControl.PropagationFlags]::InheritOnly)) { continue }
        if ($rule.IdentityReference.Value -notin $admins -and ([int]$rule.FileSystemRights -band $write)) { return $true }
    }
    return $false
}

$plugins = Join-Path $IcueDir 'plugins'
$target = Join-Path $plugins 'MsiGpuForIcue'
foreach ($path in $IcueDir, $plugins, $target, (Join-Path $target 'images')) {
    if (Test-ReparsePoint $path) { throw "$path is a link; refusing to install through it." }
}
if (Test-WritableByOthers $IcueDir) {
    Write-Warning "$IcueDir can be modified by non-administrators. Anyone who can write there can also replace iCUE itself."
}

# --- iCUE must not hold the plugin open -------------------------------------
# Third-party plugins run in iCUEDevicePluginHost.exe processes of the user;
# the service of the same name hosts Corsair's own plugins.
Get-Process -Name 'iCUE' -ErrorAction SilentlyContinue | Stop-Process -Force
$hostSvc = Get-Service -Name 'iCUEDevicePluginHost' -ErrorAction SilentlyContinue
if ($hostSvc -and $hostSvc.Status -ne 'Stopped') { Stop-Service -Name 'iCUEDevicePluginHost' -Force }
Get-Process -Name 'iCUEDevicePluginHost' -ErrorAction SilentlyContinue | Stop-Process -Force
$deadline = (Get-Date).AddSeconds(15)
while (Get-Process -Name 'iCUE', 'iCUEDevicePluginHost' -ErrorAction SilentlyContinue) {
    if ((Get-Date) -gt $deadline) { throw 'iCUE did not exit; close it (tray icon > Quit) and run the installer again.' }
    Start-Sleep -Milliseconds 250
}

# --- 1. data folder -----------------------------------------------------------
# Done first: the plugin reads settings.ini as soon as it is installed, and the
# staging folder lives here. Standard users may create folders in ProgramData,
# so an existing folder is only accepted if Administrators or SYSTEM own it and
# it is not a link. It is then locked down first and inspected afterwards, so
# nothing can be added between the check and the lock.
function New-DataFolderAcl {
    $acl = [Security.AccessControl.DirectorySecurity]::new()
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    $acl.SetAccessRuleProtection($true, $false)   # no inherited entries
    $inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    $rules = @(('S-1-5-18', 'FullControl'), ('S-1-5-32-544', 'FullControl'), ('S-1-5-32-545', 'ReadAndExecute'))
    foreach ($rule in $rules) {
        $sid = [Security.Principal.SecurityIdentifier]::new($rule[0])
        $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new($sid, $rule[1], $inherit, 'None', 'Allow'))
    }
    return $acl
}

$data = Join-Path $programData 'MsiGpuForIcue'
if (-not (Test-Path -LiteralPath $data)) {
    [void][IO.Directory]::CreateDirectory($data, (New-DataFolderAcl))   # ignores the ACL if the folder appeared meanwhile ...
}
$owner = Get-OwnerSid $data                                              # ... which this check catches
if ((Test-ReparsePoint $data) -or -not (Get-Item -LiteralPath $data -Force).PSIsContainer -or $owner -notin $trustedOwners) {
    throw "$data was not created by this installer (owner $owner). Check it, delete it and run the installer again."
}
Set-Acl -LiteralPath $data -AclObject (New-DataFolderAcl)
foreach ($item in @(Get-ChildItem -LiteralPath $data -Recurse -Force)) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "$($item.FullName) is a link; remove it and run the installer again." }
    if ((Get-OwnerSid $item.FullName) -notin $trustedOwners) {
        throw "$($item.FullName) was not created by this installer. Remove it and run the installer again."
    }
    & $icacls $item.FullName /reset /L /Q | Out-Null   # inherit the folder's ACL only
}
# Leftovers of an aborted earlier run (checked above).
Get-ChildItem -LiteralPath $data -Directory -Filter 'staging-*' -Force | Remove-Item -Recurse -Force

$ini = Join-Path $data 'settings.ini'
# Everything created here belongs to the Administrators group, not to the
# account running the script: an unelevated process of the same account would
# share that owner, and the checks above would no longer tell them apart.
function Set-AdminOwner([string]$path) {
    & $icacls $path /setowner '*S-1-5-32-544' /L /Q | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "icacls /setowner failed for $path" }
}

if (-not (Test-Path -LiteralPath $ini)) {
    Copy-Item -LiteralPath (Join-Path $root 'settings.example.ini') -Destination $ini
    Set-AdminOwner $ini
}

# --- 2. one-time certificate, sign in the admin-only staging folder -----------
$staging = Join-Path $data ('staging-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $staging | Out-Null
Set-AdminOwner $staging
$cert = $null
try {
    $stagedDll = Join-Path $staging 'MsiGpuForIcue.dll'
    Copy-Item -LiteralPath $dll -Destination $stagedDll
    Set-AdminOwner $stagedDll
    Unblock-File -LiteralPath $stagedDll   # drop the "downloaded from the internet" marker

    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
        -CertStoreLocation Cert:\LocalMachine\My -KeyExportPolicy NonExportable `
        -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10) `
        -TextExtension @('2.5.29.19={critical}{text}ca=false')   # end entity, can never act as a CA

    # Trust the public part only (RawData carries no private key), straight from memory.
    $public = [Security.Cryptography.X509Certificates.X509Certificate2]::new($cert.RawData)
    foreach ($name in 'Root', 'TrustedPublisher') {
        $store = [Security.Cryptography.X509Certificates.X509Store]::new($name, 'LocalMachine')
        $store.Open('ReadWrite')
        try { $store.Add($public) } finally { $store.Close() }
    }

    $sig = Set-AuthenticodeSignature -FilePath $stagedDll -Certificate $cert -HashAlgorithm SHA256
    $check = Get-AuthenticodeSignature -LiteralPath $stagedDll
    if ($sig.Status -ne 'Valid' -or $check.Status -ne 'Valid' -or $check.SignerCertificate.Thumbprint -ne $cert.Thumbprint) {
        throw "Signing failed: $($sig.Status) $($sig.StatusMessage)"
    }
} catch {
    # The new certificate is useless without a signed DLL: take it out of every store again.
    if ($cert) {
        foreach ($name in 'Root', 'TrustedPublisher') {
            $stale = "Cert:\LocalMachine\$name\$($cert.Thumbprint)"
            if (Test-Path -LiteralPath $stale) { Remove-Item -LiteralPath $stale }
        }
    }
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
    throw
} finally {
    # The private key is never needed again: nobody, not even an administrator,
    # can sign anything else with this certificate afterwards.
    if ($cert -and (Test-Path -LiteralPath "Cert:\LocalMachine\My\$($cert.Thumbprint)")) {
        Remove-Item -LiteralPath "Cert:\LocalMachine\My\$($cert.Thumbprint)" -DeleteKey
    }
}

# --- 3. install, then retire the certificates of earlier installations --------
try {
    New-Item -ItemType Directory -Force $target | Out-Null
    Move-Item -LiteralPath $stagedDll -Destination (Join-Path $target 'MsiGpuForIcue.dll') -Force
    # Replace the pictures as a whole so files of older versions do not linger.
    $images = Join-Path $target 'images'
    if (Test-Path -LiteralPath $images) { Remove-Item -LiteralPath $images -Recurse -Force }
    Copy-Item -Recurse -Force (Join-Path $root 'images') $target
    Get-ChildItem -LiteralPath $target -Recurse -File | Unblock-File
} finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}

$installed = Get-AuthenticodeSignature -LiteralPath (Join-Path $target 'MsiGpuForIcue.dll')
if ($installed.Status -ne 'Valid') { throw "Installed DLL has no valid signature: $($installed.Status)" }

foreach ($name in 'My', 'Root', 'TrustedPublisher') {
    Get-ChildItem "Cert:\LocalMachine\$name" |
        Where-Object { $_.Subject -eq $subject -and $_.Thumbprint -ne $cert.Thumbprint } |
        ForEach-Object {
            if ($name -eq 'My') { Remove-Item -LiteralPath $_.PSPath -DeleteKey } else { Remove-Item -LiteralPath $_.PSPath }
            Write-Host "Removed earlier certificate $($_.Thumbprint) from LocalMachine\$name"
        }
}

Write-Host ''
Write-Host "Installed to: $target"
Write-Host "Signature:    $($installed.Status), signed by $($installed.SignerCertificate.Thumbprint) (private key deleted)"
Write-Host "Valid until:  $($cert.NotAfter.ToString('d'))"
Write-Host "Settings:     $ini"
Write-Host "Log:          %LOCALAPPDATA%\MsiGpuForIcue\plugin.log (of the logged-in user)"
Write-Host ''
Write-Host 'Now start iCUE. The graphics card appears as a new device.'

} catch {
    Write-Host "ERROR: $($_.Exception.Message)"
    throw
} finally {
    if ($LogFile) { Stop-Transcript | Out-Null }
}
