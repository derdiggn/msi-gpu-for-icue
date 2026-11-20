# Development

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 or the Build Tools with the "Desktop development with
  C++" workload (MSVC, Windows SDK)
- For the coverage gate: [OpenCppCoverage](https://github.com/OpenCppCoverage/OpenCppCoverage)
  0.9.9 (`winget install OpenCppCoverage.OpenCppCoverage`)
- For the setup.exe: [Inno Setup](https://jrsoftware.org/isinfo.php) 6
  (`winget install JRSoftware.InnoSetup`)

No other dependencies; the test framework is in `tests/test_framework.h`.

## Build

```powershell
.\build.ps1             # Release (default)
.\build.ps1 -Config Debug
```

Warnings are errors (`/W4 /WX`). Outputs:

| File | Purpose |
| --- | --- |
| `build\MsiGpuForIcue.dll` | the plugin |
| `build\gputest.exe` | hardware test without iCUE |
| `build\plugintest.exe` | loads the plugin like iCUE and plays a fade |
| `build\test\tests.exe` | unit tests |
| `build\test\fake_nvapi64.dll` | NvAPI stand-in for the tests |

## Test

```powershell
.\test.ps1                    # all tests
.\test.ps1 -Filter Streamer   # tests whose name contains "Streamer"
.\test.ps1 -Coverage          # with the 100 % line-coverage gate
```

The unit tests never touch the real driver or your log: `tests.exe` loads
`fake_nvapi64.dll` and logs to `%TEMP%`. They run in about half a second.

`-Coverage` runs the tests under OpenCppCoverage, measures **line coverage of
`src\`** and fails below 100 %. The HTML report is written to
`build\coverage\index.html`. OpenCppCoverage measures lines, not branches; the
tests nevertheless exercise every error path explicitly (missing driver,
missing entry points, I2C failures, unwritable log, …).

The tools in `tools\` are manual hardware tools and are not part of the
coverage measurement.

## Packages and the setup

```powershell
.\package.ps1              # dist\msi-gpu-for-icue-<version>.zip (+ .sha256)
.\package.ps1 -Installer   # also dist\msi-gpu-for-icue-setup-<version>.exe (+ .sha256)
```

The setup (`installer/msi-gpu-for-icue.iss`, Inno Setup) copies the files to
`C:\Program Files\msi-gpu-for-icue` and runs `install.ps1` / `uninstall.ps1`
from there, so the zip and the setup share one installation code path. Its
`AppId` must never change, otherwise upgrades install side by side.

`tests\e2e\installer.ps1 -Setup <setup.exe>` installs, upgrades and
uninstalls the setup and checks the result (signature, certificate stores,
ACL of the settings folder, Apps & Features entry). It changes the machine,
needs administrator rights and refuses to run where iCUE is installed: CI runs
it on a fresh GitHub runner with a stand-in `iCUE.exe`. Run it yourself only
in a throw-away VM.

## Hardware tests

With a supported card:

```powershell
build\gputest.exe detect              # list GPUs, read only
build\gputest.exe dump                # registers 0x20..0x47, read only
build\gputest.exe static 0 0 255      # blue
build\gputest.exe direct 40 10        # 10 s rainbow at 25 fps, like the plugin
build\gputest.exe off
build\plugintest.exe build\MsiGpuForIcue.dll
```

Close iCUE first so that only one program talks to the controller. Nothing is
persisted; a reboot restores the previous lighting.

## Commits and versions

The project uses [Conventional Commits](https://www.conventionalcommits.org)
and [Semantic Versioning](https://semver.org). See
[CONTRIBUTING.md](../CONTRIBUTING.md) for the commit format.

Releases are automated with
[release-please](https://github.com/googleapis/release-please):

1. Every push to `main` updates a release pull request that bumps the version
   in `src/version.h` and `version.txt` and extends `CHANGELOG.md`.
   `fix:` and `perf:` give a patch, `feat:` a minor and `!` /
   `BREAKING CHANGE:` a major release. Other types (`docs`, `test`, `ci`, …)
   do not trigger a release.
2. Merging the release pull request tags `vX.Y.Z` and creates the GitHub
   release.
3. The same workflow builds and tests the tag with a read-only token, builds
   the zip and the setup, runs the installer end-to-end test, creates
   [build provenance attestations](https://docs.github.com/actions/security-for-github-actions/using-artifact-attestations),
   and a separate job attaches `msi-gpu-for-icue-X.Y.Z.zip`,
   `msi-gpu-for-icue-setup-X.Y.Z.exe` and their `.sha256` files to the
   release. Users can verify a download with
   `gh attestation verify <file> --repo derdiggn/msi-gpu-for-icue`.

The setup.exe is not Authenticode-signed (that needs a commercial or a
sponsored OSS code-signing certificate), so SmartScreen shows an "unknown
publisher" warning; the attestation is the way to verify it. The plugin DLL
is signed on each machine during installation, see the README.

Never edit the version numbers by hand.

Repository settings this relies on: *Settings → Actions → General → Allow
GitHub Actions to create and approve pull requests* must be enabled. Pull
requests opened with `GITHUB_TOKEN` do not trigger other workflows, so CI does
not run on the release PR; do not make CI a required status check for `main`
(or close and reopen the release PR to run it).

## CI

`.github/workflows/ci.yml` runs on every push and pull request:

- build with `/W4 /WX`, unit tests, the 100 % coverage gate, zip and setup
- the installer end-to-end test on a fresh runner
- PSScriptAnalyzer on all PowerShell scripts
- commitlint (`.commitlintrc.json`) on every commit of a pull request
- `pr-title.yml`: the PR title must be a Conventional Commit (squash merges
  use it as the commit message); re-runs when the title is edited

## Dependency updates

[Renovate](https://docs.renovatebot.com) (`renovate.json`) keeps these up to
date, every Monday morning, after a release has been public for 3 days:

- GitHub Actions, pinned to commit SHAs (one grouped PR, `ci(deps): …`)
- the CI tools OpenCppCoverage, Inno Setup (Chocolatey) and PSScriptAnalyzer
  (PowerShell Gallery), declared once per workflow under `env:` with a
  `# renovate:` comment (`build(deps): …`)

Renovate opens the PRs automatically but **never merges them**. The release
workflow does not run on pull requests, and a backdoored compiler or action
would pass CI anyway, so a human reviews each update (changelog, new SHA)
before squash-merging it. Releases without a publish timestamp are not
proposed (`minimumReleaseAgeBehaviour: timestamp-required`). The commit types
are hidden from the changelog, so dependency updates alone never cause a
release. There are no library dependencies: the plugin only links Windows
system libraries.

The release build additionally checks that `ISCC.exe` is signed by the Inno
Setup publisher before compiling the setup.

New pinned tools must get the same `# renovate:` comment, otherwise they are
never updated.
