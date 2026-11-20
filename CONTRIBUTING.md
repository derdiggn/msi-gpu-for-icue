# Contributing

Thanks for helping! Bug reports, reports for untested cards and pull requests
are all welcome.

## Reporting a card

If you own one of the cards marked as untested in the README, open a
[card report](../../issues/new?template=card_report.yml) with the output of:

```powershell
build\gputest.exe detect
build\gputest.exe direct 40 10      # close iCUE first
```

## Pull requests

1. Fork, create a branch, make your change.
2. `.\build.ps1` must build without warnings and `.\test.ps1 -Coverage` must
   pass, including the 100 % line-coverage gate. New code comes with tests;
   see [docs/development.md](docs/development.md).
3. Update the documentation when behaviour changes.
4. Open the pull request. Its **title** must be a Conventional Commit, because
   it becomes the commit message on `main` (squash merge). CI also lints every
   commit of the pull request with commitlint.

Do not bump version numbers or edit `CHANGELOG.md`; release-please does that.

## Commit messages

This project follows [Conventional Commits 1.0.0](https://www.conventionalcommits.org):

```
<type>(<optional scope>): <description>

<optional body>

<optional footer(s)>
```

| Type | Use for | Release |
| --- | --- | --- |
| `feat` | a new feature for users | minor |
| `fix` | a bug fix for users | patch |
| `perf` | a performance improvement | patch |
| `docs` | documentation only | none |
| `refactor` | code change without behaviour change | none |
| `test` | tests only | none |
| `build` | build scripts, compiler flags | none |
| `ci` | GitHub workflows | none |
| `chore` | maintenance that fits nowhere else | none |
| `style` | formatting only | none |
| `revert` | reverting an earlier commit | depends |

A `!` after the type/scope or a `BREAKING CHANGE:` footer makes a major
release, for example when settings keys or the device id change.

Common scopes: `plugin`, `streamer`, `controller`, `nvapi`, `settings`,
`installer`, `tools`, `tests`, `release`.

Examples:

```
feat(controller): support RTX 5070 Ti VENTUS
fix(streamer): keep the last colour of a fade
docs: explain the LED id mapping
```

To check messages locally, enable the hook once per clone:

```powershell
git config core.hooksPath .githooks
```

## Code style

- C++17, MSVC, `/W4 /WX`, no exceptions across the plugin ABI.
- 4 spaces, braces on their own line for functions, 120 columns.
- Comments explain *why*; protocol knowledge belongs in `docs/protocol.md`.
- Hardware access only through `nvapi_i2c`; streaming timing goes through a
  `Clock`, so the tests can control time.
- Everything iCUE calls is wrapped in `Guarded()` (`src/abi_guard.h`).
- Description in lower case, no full stop at the end (commitlint rules).

## License

By contributing you agree that your contribution is licensed under
GPL-2.0-or-later, like the rest of the project.
