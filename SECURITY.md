# Security policy

## Supported versions

Only the latest release receives fixes.

## Reporting a vulnerability

Please **do not** open a public issue. Use GitHub's
[private vulnerability reporting](../../security/advisories/new) instead. You
will get an answer within a week.

## Security model

The main attacker considered is code running **without elevation in the
account of the person who installs** (the usual Windows setup, where the same
account approves the UAC prompt), plus other standard users of the machine.
Neither must be able to gain administrator rights through the installer, or
make Windows trust code they wrote.

- **One-time signing certificate.** iCUE only loads plugins with a signature
  Windows trusts. Each installation therefore creates a self-signed
  code-signing certificate, signs `MsiGpuForIcue.dll` with it and **deletes the
  private key in the same run**; afterwards not even an administrator can sign
  anything else with it. Only the public part stays, in `LocalMachine\Root`
  and `LocalMachine\TrustedPublisher`. It is an end entity (basic constraints
  `CA=false`, key usage *digital signature*, extended key usage *code
  signing*), so it cannot issue certificates, cannot authenticate TLS servers
  and cannot sign kernel drivers. Certificates of earlier installations are
  removed only after the new DLL is signed and installed; uninstalling removes
  the certificate in any case, even when another uninstall step fails.
- **Elevated scripts trust nothing the user controls.** `install.ps1` and
  `uninstall.ps1` first pin `PSModulePath` to the system modules, disable
  module auto-loading and import the modules they need by full path (the
  user's module folder comes first by default and is writable without
  elevation). Tools such as `icacls.exe` are called by full path, and system
  folders are resolved through Windows (`Environment.GetFolderPath`), never
  through environment variables a user can override.
- **No user-writable staging.** Nothing is staged in `%TEMP%` or other
  locations a standard user can write. The public certificate is added to the
  stores from memory, and the DLL is signed in a randomly named folder inside
  the administrator-only settings folder before it is moved into iCUE's plugin
  folder. Links (junctions, symbolic links) in the iCUE and plugin folders are
  refused.
- **Settings folder.** `%ProgramData%\MsiGpuForIcue` is created atomically
  with an ACL that lets only administrators and SYSTEM write and with the
  Administrators group as owner, before anything else is installed. Standard
  users may create folders in `%ProgramData%`, so a pre-existing folder is only
  accepted if Administrators or SYSTEM own it and it is not a link; it is then
  locked down, and anything inside that is a link or not owned by
  Administrators/SYSTEM is rejected. `uninstall.ps1 -RemoveData` applies the
  same checks before deleting.
- **Setup.** The setup.exe (Inno Setup) installs its scripts to
  `C:\Program Files\msi-gpu-for-icue` and runs them from there with
  `powershell.exe -NoProfile -File`, so paths are passed as plain arguments and
  never parsed as PowerShell code. Its logs are written to that folder, not to
  `%TEMP%`. The iCUE folder for the uninstaller is stored in HKLM.
- **Zip package.** The zip runs the same scripts, but from wherever you
  extract it. Extract it to a folder only administrators can write and start
  it with `-NoProfile`, as described in the README; otherwise a program of
  your account could change the files before you run them elevated. The setup
  avoids this by design.
- **Release files.** The setup.exe and the zip are built by GitHub Actions
  from the tagged source and carry build provenance attestations
  (`gh attestation verify <file> --repo derdiggn/msi-gpu-for-icue`). The build
  job has a read-only token and no attestation rights; a separate job that
  runs no build tools attests and publishes the files. The Inno Setup compiler
  is checked for its publisher's signature before use. The setup.exe itself
  is not Authenticode-signed, so SmartScreen reports an unknown publisher.
- **Dependency updates.** Renovate opens update PRs for the SHA-pinned GitHub
  Actions and the pinned CI tools, but never merges them: a compromised
  update to the release toolchain would not be caught by CI, so every update
  needs a human review. Releases younger than 3 days are not proposed.
- **Plugin.** The plugin runs inside iCUE's plugin host as the logged-in user
  and needs no elevation. `nvapi64.dll` is loaded from System32 only
  (`LOAD_LIBRARY_SEARCH_SYSTEM32`). Every function iCUE calls is wrapped in an
  exception barrier, LED ids and colours are range-checked, `settings.ini`
  values are clamped and only implemented feature bits are reported. The DLL
  is built with Control Flow Guard (`/guard:cf`), ASLR and DEP. The log is
  written to the user's own `%LOCALAPPDATA%`.
- **Hardware.** The plugin only writes the mode (0x22), flag (0x2D, 0x2E),
  colour (0x30–0x32), brightness (0x36) and speed (0x38) registers of the RGB
  controller and never the "save" register (0x3F), so a reboot always restores
  the previous state.

## Known limitations

- The setup.exe has no commercial code-signing certificate (SmartScreen
  warning); verify downloads with the attestation.
- If iCUE itself is installed in a folder that non-administrators can modify
  (only possible with a custom location), they can replace iCUE regardless of
  this plugin. The installer warns about it.
- The plugin's signature has no timestamp and becomes invalid when the
  certificate expires after 10 years; installing a newer version renews it.
