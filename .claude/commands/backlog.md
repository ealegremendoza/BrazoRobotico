---
description: List pending backlog tasks and the last 3 completed ones from doc/md/BACKLOG.md
allowed-tools: Read, Bash(git log:*), Bash(rg:*)
---

## Context

- Backlog file: @doc/md/BACKLOG.md
- Completion history (newest first; first `+- [x]` occurrence per ID = when it was marked done):
!`git log -p --format='COMMIT %ad' --date=short -- doc/md/BACKLOG.md | rg -o '^(COMMIT .*|\+- \[x\] \*\*\[[A-Z]+\d+\]\*\*)' | head -60`

## Task

Show the user (reply in Spanish, short) two lists:

1. **Pendientes**: every `- [ ]` task in BACKLOG.md, grouped by category (keep the emoji headers), with its ID and a one-line summary. Skip placeholder items like "(agregar tareas aquí)". Mention dependencies ("Depende de ...") when present.
2. **Últimas 3 realizadas**: the 3 most recently completed `[x]` task IDs according to the git history above (use the first time each ID appears as `+- [x]`, newest first), with the date and a one-line summary. If an `[x]` task is not yet committed, it counts as the most recent.

Rules:
- This command is READ-ONLY. Never modify BACKLOG.md here.
- Only the user decides what to add to or remove from the backlog; never add, remove or mark tasks on your own initiative.
- End by asking in one line whether they want to add, update or remove anything.
