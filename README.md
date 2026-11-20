<img src="assets/icon.svg" width="64" height="64" alt="">

# msi-gpu-for-icue

[![ci](https://github.com/derdiggn/msi-gpu-for-icue/actions/workflows/ci.yml/badge.svg)](https://github.com/derdiggn/msi-gpu-for-icue/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/derdiggn/msi-gpu-for-icue)](https://github.com/derdiggn/msi-gpu-for-icue/releases)
![coverage](https://img.shields.io/badge/line%20coverage-100%25-brightgreen)
[![license](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](LICENSE)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-yellow.svg)](https://www.conventionalcommits.org)

A native Corsair iCUE 5 plugin for the RGB lighting of MSI GeForce RTX 50
series graphics cards. The card shows up in iCUE as a regular device with its
own picture, and you control it like any Corsair device: effects, Lighting
Link and profiles.

The plugin talks to the card's RGB controller directly through the NVIDIA
driver (I2C over NvAPI). It does not need OpenRGB, SignalRGB or MSI Center
running in the background, and it does not patch or modify iCUE.

> **Unofficial project.** Not affiliated with, endorsed by or supported by
> Corsair, MSI or NVIDIA. It relies on undocumented interfaces, so use it at
> your own risk.

Tested with: MSI GeForce RTX 5080 SUPRIM LIQUID SOC, iCUE 5.52, NVIDIA driver
32.0.16.1742 (617.42), Windows 11.

## Contents

- [Supported cards](#supported-cards)
- [Installation](#installation)
- [Uninstall](#uninstall)
- [Settings and log](#settings-and-log)
- [Troubleshooting](#troubleshooting)
- [Notes](#notes)
- [Documentation](#documentation)
- [Credits and license](#credits-and-license)

## Supported cards

Every card that OpenRGB drives with its "MSI GPU v2" controller:

| Card | PCI device | PCI subsystem | Picture | Tested |
| --- | --- | --- | --- | --- |
| RTX 5090 SUPRIM LIQUID SOC | 10DE:2B85 | 1462:5300 | SUPRIM LIQUID | |
| RTX 5090 SUPRIM SOC | 10DE:2B85 | 1462:5301 | SUPRIM | |
| RTX 5090 VANGUARD SOC | 10DE:2B85 | 1462:5302 | VANGUARD | |
| RTX 5090 Gaming Trio OC | 10DE:2B85 | 1462:5303 | Gaming Trio | |
| RTX 5080 SUPRIM LIQUID SOC | 10DE:2C02 | 1462:5312 | SUPRIM LIQUID | ✅ |
| RTX 5080 SUPRIM SOC | 10DE:2C02 | 1462:5313 | SUPRIM | |
| RTX 5080 VANGUARD SOC | 10DE:2C02 | 1462:5314 | VANGUARD | |
| RTX 5080 Gaming Trio OC | 10DE:2C02 | 1462:5315 | Gaming Trio | |
| RTX 5070 Ti VANGUARD SOC | 10DE:2C05 | 1462:5314 | VANGUARD | |
| RTX 5070 Ti Gaming Trio OC Plus | 10DE:2C05 | 1462:5315 | Gaming Trio | |
| RTX 5070 Gaming Trio | 10DE:2F04 | 1462:5322 | Gaming Trio | |
| RTX 5060 Ti 16G Gaming Trio OC | 10DE:2D04 | 1462:5353 | Gaming Trio | |
| RTX 5060 Ti Gaming OC | 10DE:2D04 | 1462:5354 | Gaming (two fans) | |
| RTX 5060 8G Gaming Trio OC | 10DE:2D05 | 1462:5372 | Gaming Trio | |

The whole card always shows a single colour. The controller has no
individually addressable LEDs, so iCUE shows one lighting zone.

iCUE shows a picture of the card's design family (column "Picture"). The
pictures are original drawings made by `tools/make-images.ps1`, not
manufacturer photos.

Own one of the untested cards? Please send a
[card report](../../issues/new?template=card_report.yml), working or not.

## Installation

### Option A: setup (recommended)

1. Download `msi-gpu-for-icue-setup-<version>.exe` from the
   [Releases](../../releases) page.
2. Run it and confirm the administrator prompt. Windows SmartScreen may warn
   about an unknown publisher, because the setup itself is not signed with a
   commercial certificate: choose *More info → Run anyway*, or verify the
   download first (see below).
3. At the end, let the setup start iCUE. The graphics card appears as a new
   device.

To update, run the setup of the new version. To uninstall, use *Settings →
Apps → Installed apps → msi-gpu-for-icue*; it also removes the local
certificate and asks whether to keep your settings.

Silent installation: `msi-gpu-for-icue-setup-<version>.exe /VERYSILENT`. If
iCUE is not in its default folder, add `/ICUEDIR="D:\path\to\iCUE5"`.

**Verifying a download.** Every release file is built by GitHub Actions from
the tagged source and has a
[build provenance attestation](https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations):

```powershell
gh attestation verify .\msi-gpu-for-icue-setup-<version>.exe --repo derdiggn/msi-gpu-for-icue
```

The `.sha256` files next to the downloads contain the SHA-256 of each file
(`Get-FileHash <file>`).

### Option B: zip package

1. Download `msi-gpu-for-icue-<version>.zip` from the
   [Releases](../../releases) page and extract it to a folder **only
   administrators can write**, for example `C:\Program Files\msi-gpu-for-icue`.
   The script runs elevated, so a folder like `Downloads` would let any
   program of your account change it before it runs. The setup (option A)
   takes care of this by itself.
2. Open Windows PowerShell **as administrator** in that folder and run
   (PowerShell 7 / `pwsh` is not supported by the scripts; `-NoProfile`
   keeps your user profile script out of the elevated session):

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
   ```

3. Start iCUE. The graphics card appears as a new device.

### Option C: build from source

Requirements: Visual Studio 2022 or the Build Tools with the "Desktop
development with C++" workload.

```powershell
.\build.ps1
.\test.ps1
```

Then build the setup with `.\package.ps1 -Installer` and run it, or run
`install.ps1` as in option B. See [docs/development.md](docs/development.md)
for details.

### What the installer does

The setup copies its files to `C:\Program Files\msi-gpu-for-icue` and runs
`install.ps1` from there; the zip runs the same script directly. It:

1. Closes iCUE and its plugin host processes.
2. Creates `%ProgramData%\MsiGpuForIcue\settings.ini`, writable only by
   administrators.
3. Creates a one-time code-signing certificate, signs `MsiGpuForIcue.dll`
   with it in an administrator-only staging folder and deletes the private
   key right away (see below).
4. Moves the DLL with its pictures to
   `C:\Program Files\Corsair\Corsair iCUE5 Software\plugins\MsiGpuForIcue`
   (another location with `-IcueDir`) and removes the certificates of earlier
   installations. It prints the signature status at the end.

Run it again to update; settings are kept. If signing fails, the installed
version stays untouched.

### Why a local certificate?

iCUE only loads DLLs from its plugin folder whose Authenticode signature
Windows trusts. That also applies to other vendors' files, for example the
MSI-signed `MysticLight_SDK_x64.dll`. iCUE does not require a Corsair
signature, just a trusted one. The installer therefore signs the plugin on
your machine:

- Each installation creates a new certificate, signs the DLL and **deletes
  the private key immediately**. Nobody, not even an administrator, can sign
  anything else with it afterwards.
- Only the public certificate stays, in `LocalMachine\Root` and
  `LocalMachine\TrustedPublisher`. It is an end-entity code-signing
  certificate (`CA=false`) and cannot issue other certificates, so it cannot
  be used for TLS interception or drivers.

Nothing in iCUE is modified and no signature check is bypassed. Uninstalling
removes the certificate again. See [SECURITY.md](SECURITY.md) for the security
model.

## Uninstall

Installed with the setup: *Settings → Apps → Installed apps →
msi-gpu-for-icue → Uninstall*.

Installed from the zip, from an elevated PowerShell in the same folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\uninstall.ps1 -RemoveData
```

The certificate is always removed (unless you pass `-KeepCertificate`); without
`-RemoveData` your `settings.ini` is kept.

## Settings and log

- `%ProgramData%\MsiGpuForIcue\settings.ini`: optional settings, editing
  requires administrator rights (see
  [settings.example.ini](settings.example.ini)). Changes take effect the next
  time iCUE starts.
- `%LOCALAPPDATA%\MsiGpuForIcue\plugin.log`: what the plugin detected at
  startup. It starts over at 1 MB.

| Key | Default | Range | Meaning |
| --- | --- | --- | --- |
| `FrameMs` | `33` | 10–1000 | Minimum time between two colour updates in ms (33 ≈ 30 fps). |
| `TurnOffOnExit` | `0` | 0/1 | `1` switches the card's LEDs off when iCUE quits. |
| `Verbose` | `0` | 0/1 | `1` logs every colour change. |

## Troubleshooting

| Symptom | Check |
| --- | --- |
| The card does not appear in iCUE | `plugin.log` exists? If not, iCUE did not load the plugin: run `install.ps1` again and check that it ends with `Signature: Valid`. If it exists, look at the `detect:` line. The plugin keeps retrying detection, so a card that shows up late (driver start) is still found. |
| `detect: no supported MSI card found` | Your card's ids are listed after `GPUs:`. Please open a [card report](../../issues/new?template=card_report.yml). |
| The card appears but the colour never changes | Make sure MSI Center / Mystic Light is not controlling the card. Check the log for `write failed`; set `Verbose=1` to log every frame. |
| Lighting stops after sleep | The plugin re-initialises after a pause of more than 5 s. If it still fails, please open a [bug report](../../issues/new?template=bug_report.yml) with the log. |

`build\gputest.exe detect` (part of the release zip) shows what the plugin
sees without iCUE.

## Notes

- The plugin never writes to the card's "save" register. Without iCUE the
  card keeps the last colour until the next reboot and then shows whatever was
  last saved permanently, for example with MSI Center.
- MSI Center / Mystic Light should not control the graphics card at the same
  time.
- iCUE's plugin interface is not officially documented. A major iCUE update
  may change it and break the plugin.

## Documentation

| Document | Content |
| --- | --- |
| [docs/architecture.md](docs/architecture.md) | Modules, threads, life cycle, streaming rules |
| [docs/protocol.md](docs/protocol.md) | NvAPI I2C access and the MSI GPU v2 register protocol |
| [docs/icue-plugin-abi.md](docs/icue-plugin-abi.md) | The reverse-engineered iCUE 5 device-plugin interface |
| [docs/development.md](docs/development.md) | Build, tests, coverage, hardware tests, releases |
| [CONTRIBUTING.md](CONTRIBUTING.md) | How to contribute, Conventional Commits |
| [SECURITY.md](SECURITY.md) | Reporting vulnerabilities, security model |
| [AGENTS.md](AGENTS.md) | Instructions for AI coding agents ([CLAUDE.md](CLAUDE.md) and `.claude/` for Claude Code) |
| [CHANGELOG.md](CHANGELOG.md) | Release history (generated by release-please) |

## Credits and license

The controller protocol builds on the reverse-engineering work of
[OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) and
[SignalRGB](https://gitlab.com/signalrgb/signal-plugins), the iCUE plugin
structures on [CUEORGBPlugin](https://github.com/expired6978/CUEORGBPlugin).
Details in [THIRD_PARTY.md](THIRD_PARTY.md).

Copyright © 2026 derdiggn and the msi-gpu-for-icue contributors.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version. See [LICENSE](LICENSE).

Corsair and iCUE are trademarks of Corsair Memory, Inc. MSI and Mystic Light
are trademarks of Micro-Star International Co., Ltd. NVIDIA and GeForce are
trademarks of NVIDIA Corporation.
