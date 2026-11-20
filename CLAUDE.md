@AGENTS.md

## Claude Code

- The shared setup lives in `.claude/`: `settings.json` (permissions) and the
  `verify` skill. Personal overrides belong in `.claude/settings.local.json` or
  `CLAUDE.local.md`; both are git-ignored.
- Run `/verify` before reporting a change as done. It builds, runs the tests
  with the coverage gate and lints the scripts.
- `settings.json` allows the build and test scripts, asks before hardware
  tools and `git push`, and denies the installer scripts. These rules are
  guardrails, not a sandbox: the safety rules in AGENTS.md still apply.
- Use the PowerShell tool for the `.ps1` scripts; Git Bash is fine for git and
  text tools.
