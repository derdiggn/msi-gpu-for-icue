# SPDX-License-Identifier: GPL-2.0-or-later
#
# End-to-end test of the setup.exe: install, upgrade and uninstall on a machine
# WITHOUT iCUE (a stand-in iCUE.exe is created and removed again). It changes
# the machine's certificate stores, Program Files and ProgramData, so run it
# only on a disposable machine: CI runs it on a fresh GitHub runner.
#
#   .\tests\e2e\installer.ps1 -Setup dist\msi-gpu-for-icue-setup-<version>.exe
#Requires -RunAsAdministrator
param(
    [Parameter(Mandatory)]
    [string]$Setup,
    [string]$LogDir = (Join-Path $env:TEMP 'msi-gpu-for-icue-e2e')
)

$ErrorActionPreference = 'Stop'
$Setup = (Resolve-Path $Setup).Path
$icue = "$env:ProgramFiles\Corsair\Corsair iCUE5 Software"
$plugin = Join-Path $icue 'plugins\MsiGpuForIcue'
$app = "$env:ProgramFiles\msi-gpu-for-icue"
$data = "$env:ProgramData\MsiGpuForIcue"
$subject = 'CN=msi-gpu-for-icue local plugin signing'
$uninstallKey = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{E60C48C5-92DB-4282-99FA-0FFB0A47B9FF}_is1'
$logs = $LogDir
New-Item -ItemType Directory -Force $logs | Out-Null

function Assert-True([bool]$condition, [string]$what) {
    if (-not $condition) { throw "FAILED: $what" }
    Write-Host "ok   $what"
}

function Invoke-Setup([string]$exe, [string]$log, [string[]]$extra = @()) {
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"") + $extra
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru
    return $p.ExitCode
}

function Get-OurCertificate([string]$store) {
    @(Get-ChildItem "Cert:\LocalMachine\$store" | Where-Object Subject -eq $subject)
}

if (Test-Path (Join-Path $icue 'iCUE.exe')) {
    throw 'iCUE is installed on this machine. Run this test only on a disposable machine without iCUE.'
}

try {
    # --- without iCUE the setup refuses to install ---------------------------
    $code = Invoke-Setup $Setup "$logs\setup-no-icue.log"
    Assert-True ($code -ne 0) "setup without iCUE fails (exit code $code)"
    Assert-True (-not (Test-Path $app)) 'nothing installed without iCUE'

    # --- install -------------------------------------------------------------
    New-Item -ItemType Directory -Force $icue | Out-Null
    Copy-Item "$env:SystemRoot\System32\whoami.exe" (Join-Path $icue 'iCUE.exe')   # stand-in

    $code = Invoke-Setup $Setup "$logs\setup-install.log"
    if ($code -ne 0 -and (Test-Path "$app\install.log")) { Get-Content "$app\install.log" | Write-Host }
    Assert-True ($code -eq 0) "install exit code 0 (got $code)"

    $dll = Join-Path $plugin 'MsiGpuForIcue.dll'
    $sig = Get-AuthenticodeSignature -LiteralPath $dll
    Assert-True ($sig.Status -eq 'Valid') "plugin DLL has a valid signature ($($sig.Status))"
    Assert-True ($sig.SignerCertificate.Subject -eq $subject) 'plugin DLL is signed by the local certificate'
    $pictures = @(Get-ChildItem -LiteralPath (Join-Path $plugin 'images') -Recurse -Filter *.png)
    $sourcePictures = @(Get-ChildItem -LiteralPath (Join-Path $app 'images') -Recurse -Filter *.png)
    Assert-True ($pictures.Count -gt 0 -and $pictures.Count -eq $sourcePictures.Count) "all $($sourcePictures.Count) pictures installed"
    Assert-True (@(Get-ChildItem -LiteralPath $data -Directory -Filter 'staging-*').Count -eq 0) 'staging folder removed'
    Assert-True (-not (Test-Path (Join-Path $icue 'MsiGpuForIcue.staging'))) 'nothing staged in the iCUE folder'

    Assert-True ((Get-OurCertificate 'My').Count -eq 0) 'no signing key left behind (one-time certificate)'
    foreach ($store in 'Root', 'TrustedPublisher') {
        $public = Get-OurCertificate $store
        Assert-True ($public.Count -eq 1 -and -not $public[0].HasPrivateKey) "public certificate only in LocalMachine\$store"
        Assert-True ($public[0].Thumbprint -eq $sig.SignerCertificate.Thumbprint) "LocalMachine\$store holds the signer"
        $constraints = $public[0].Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.19' }
        Assert-True ($constraints -and -not $constraints.CertificateAuthority) "certificate in $store is an end entity (CA=false)"
    }

    Assert-True (Test-Path "$data\settings.ini") 'settings.ini created'
    $acl = Get-Acl -LiteralPath $data
    Assert-True $acl.AreAccessRulesProtected 'settings folder does not inherit permissions'
    Assert-True ($acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -eq 'S-1-5-32-544') 'settings folder owned by Administrators'
    $iniOwner = (Get-Acl -LiteralPath "$data\settings.ini").GetOwner([Security.Principal.SecurityIdentifier]).Value
    Assert-True ($iniOwner -in 'S-1-5-32-544', 'S-1-5-18') "settings.ini owned by Administrators or SYSTEM ($iniOwner)"
    $write = [int][Security.AccessControl.FileSystemRights]'WriteData, AppendData, WriteExtendedAttributes, WriteAttributes, Delete' -bor 0x50000000
    $usersCanWrite = $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier]) | Where-Object {
        $_.AccessControlType -eq 'Allow' -and
            $_.IdentityReference.Value -notin 'S-1-5-18', 'S-1-5-32-544' -and ([int]$_.FileSystemRights -band $write)
    }
    Assert-True (-not $usersCanWrite) 'only administrators and SYSTEM can write the settings folder'
    Assert-True (Test-Path $uninstallKey) 'entry in Apps & Features'
    Assert-True (Test-Path "$app\install.log") 'install log written to the program folder'

    # --- upgrade: new one-time certificate replaces the old one, settings kept -
    Add-Content "$data\settings.ini" "`r`n; kept across upgrades"
    $oldThumbprint = $sig.SignerCertificate.Thumbprint
    $code = Invoke-Setup $Setup "$logs\setup-upgrade.log"
    Assert-True ($code -eq 0) "second install (upgrade) exit code 0 (got $code)"
    $newSig = Get-AuthenticodeSignature -LiteralPath $dll
    Assert-True ($newSig.Status -eq 'Valid' -and $newSig.SignerCertificate.Thumbprint -ne $oldThumbprint) 'upgrade signs with a new certificate'
    foreach ($store in 'Root', 'TrustedPublisher') {
        $left = Get-OurCertificate $store
        Assert-True ($left.Count -eq 1 -and $left[0].Thumbprint -eq $newSig.SignerCertificate.Thumbprint) "old certificate removed from LocalMachine\$store"
    }
    Assert-True ((Get-OurCertificate 'My').Count -eq 0) 'still no signing key after the upgrade'
    Assert-True ((Get-Content "$data\settings.ini" -Raw) -match 'kept across upgrades') 'settings kept on upgrade'

    # --- uninstall -----------------------------------------------------------
    $code = Invoke-Setup "$app\unins000.exe" "$logs\uninstall.log"
    Assert-True ($code -eq 0) "uninstall exit code 0 (got $code)"
    $deadline = (Get-Date).AddSeconds(60)   # the uninstaller finishes from a temporary copy
    while ((Test-Path $uninstallKey) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }

    Assert-True (-not (Test-Path $plugin)) 'plugin removed from iCUE'
    Assert-True (-not (Test-Path $uninstallKey)) 'Apps & Features entry removed'
    foreach ($store in 'My', 'Root', 'TrustedPublisher') {
        Assert-True ((Get-OurCertificate $store).Count -eq 0) "certificate removed from LocalMachine\$store"
    }
    Assert-True (Test-Path "$data\settings.ini") 'silent uninstall keeps settings.ini'
    Write-Host ''
    Write-Host 'installer end-to-end test passed'
} finally {
    Remove-Item -LiteralPath $icue -Recurse -Force -ErrorAction SilentlyContinue
}
