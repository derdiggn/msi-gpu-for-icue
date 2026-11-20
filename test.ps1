# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs the unit tests (build them first with .\build.ps1).
#
#   .\test.ps1                      run all tests
#   .\test.ps1 -Filter Streamer     run tests whose name contains "Streamer"
#   .\test.ps1 -Coverage            run under OpenCppCoverage and fail below
#                                   100 % line coverage of src\
#
# -Coverage needs OpenCppCoverage (https://github.com/OpenCppCoverage/OpenCppCoverage)
# on PATH or passed with -OpenCppCoverage. The HTML report ends up in
# build\coverage\index.html, the Cobertura XML in build\coverage.xml.
param(
    [string]$Filter = '',
    [switch]$Coverage,
    [string]$OpenCppCoverage = 'OpenCppCoverage.exe',
    [double]$MinimumLineRate = 1.0
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$exe = Join-Path $root 'build\test\tests.exe'
if (-not (Test-Path $exe)) { throw "Not found: $exe  (run .\build.ps1 first)" }

$testArgs = @()
if ($Filter) { $testArgs += $Filter }

if (-not $Coverage) {
    & $exe @testArgs
    exit $LASTEXITCODE
}

$xml = Join-Path $root 'build\coverage.xml'
$html = Join-Path $root 'build\coverage'
Remove-Item -Recurse -Force $xml, $html -ErrorAction SilentlyContinue

& $OpenCppCoverage --quiet `
    --modules $exe `
    --sources (Join-Path $root 'src') `
    --export_type "cobertura:$xml" `
    --export_type "html:$html" `
    --cover_children `
    -- $exe @testArgs
if ($LASTEXITCODE -ne 0) { throw "tests failed (exit code $LASTEXITCODE)" }

# Per-file summary from the Cobertura report.
[xml]$report = Get-Content $xml
$total = 0; $hit = 0
$rows = foreach ($class in $report.coverage.packages.package.classes.class) {
    $lines = @($class.lines.line)
    $covered = @($lines | Where-Object { [int]$_.hits -gt 0 }).Count
    $total += $lines.Count; $hit += $covered
    $missing = ($lines | Where-Object { [int]$_.hits -eq 0 } | ForEach-Object { $_.number }) -join ','
    [pscustomobject]@{ File = $class.filename; Lines = $lines.Count; Covered = $covered; Missing = $missing }
}
$rows | Sort-Object File | Format-Table -AutoSize | Out-String -Width 200 | Write-Host

$rate = if ($total) { $hit / $total } else { 0 }
Write-Host ('Line coverage of src\: {0:P2} ({1}/{2})' -f $rate, $hit, $total)
if ($rate -lt $MinimumLineRate) {
    throw ('Line coverage {0:P2} is below the required {1:P0}' -f $rate, $MinimumLineRate)
}
