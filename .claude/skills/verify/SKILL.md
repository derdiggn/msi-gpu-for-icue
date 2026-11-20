---
name: verify
description: Build msi-gpu-for-icue, run the unit tests with the 100 % line-coverage gate and lint the PowerShell scripts. Use before reporting any code change as done, or when asked to check that the project is green.
---

# Verify

Run from the repository root with the PowerShell tool and stop at the first
failure.

1. Build (warnings are errors):

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
   ```

2. Unit tests with the coverage gate. Find OpenCppCoverage on PATH, or under
   `$env:ProgramFiles\OpenCppCoverage\OpenCppCoverage.exe`. If it is not
   installed, run the tests without `-Coverage` and say clearly that coverage
   was not measured.

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\test.ps1 -Coverage -OpenCppCoverage <path>
   ```

   On a coverage failure, the table lists the missing line numbers per file.
   Add tests for them; never lower the threshold or exclude files.

3. Lint the scripts if PSScriptAnalyzer is available
   (`Get-Module -ListAvailable PSScriptAnalyzer`), with the same settings as CI:

   ```powershell
   Invoke-ScriptAnalyzer -Path . -Recurse -Severity Error, Warning -ExcludeRule PSAvoidUsingWriteHost, PSUseShouldProcessForStateChangingFunctions
   ```

4. Report: build result, number of tests and failures, the coverage figure,
   and lint findings. Quote failures verbatim.

Never run `build\gputest.exe`, `build\plugintest.exe`, `install.ps1` or
`uninstall.ps1` as part of verification.
