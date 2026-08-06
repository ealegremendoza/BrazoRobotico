# Repository Hygiene

## Purpose

Keep the git repository free of machine-local and regenerable artifacts, so
clones stay small and portable.

## Requirements

### REQ-HYG-001: Never commit the virtualenv
The Python virtualenv under `STServo_Python/stservo-env/` is regenerable via
`requirements.txt` and machine-specific (absolute paths, platform wheels).
It is ignored by `.gitignore` and must never be tracked.

#### Scenario: A new clone
- **Given** a fresh clone of the repository
- **When** the developer creates `STServo_Python/stservo-env` with
  `python -m venv` and installs `STServo_Python/requirements.txt`
- **Then** the working tree matches the tracked files with no venv bloat

### REQ-HYG-002: .gitignore only affects untracked files
Ignore rules have no effect on already-tracked files. If a file was
committed before its ignore rule existed, it must be removed from the index
with `git rm --cached` (keeping it on disk), then committed.

#### Scenario: A tracked file matches an ignore rule
- **Given** `.vscode/browse.vc.db*` was committed before being added to
  `.gitignore`
- **When** `git rm --cached` is run on those files and committed
- **Then** the files stay on disk but stop appearing as modified

### REQ-HYG-003: Ignored content in this repository
The `.gitignore` ignores `STServo_Python/stservo-env/`, `**/__pycache__/`,
`.vscode/browse.vc.db*`, and `.atl/` (local tooling cache).

## Design

- Tracked Python entry points live outside the venv (e.g.
  `STServo_Python/servo_control_ui.py`), importing the SDK from
  `stservo-env/` via a `sys.path` insert.
- Use `git check-ignore -v <path>` to verify why a path is ignored and
  `git check-ignore --no-index` to test rules against tracked paths.
