# Archived Ralph Loop Setup For Marrow

> Historical record only. `.agents/ralph/`, `.ralph/`, and the commands below are preserved to explain earlier automation runs. They are not current execution instructions, milestone authority, or commit policy. Current architecture and dependency checkpoints live in [`discription.md`](discription.md) and `.agents/tasks/prd-marrow-runtime.json`.

This repository previously used a project-local Ralph configuration wired to Codex.

## Historical defaults

- Ralph config: `.agents/ralph/config.sh`
- PRD: `.agents/tasks/prd-marrow-runtime.json`
- State directory: `.ralph/`
- Agent runner: `codex exec --full-auto -`
- Commit mode at the time: enabled because this folder had a `.git/` directory

## Historical commands (do not use as the current workflow)

```bash
ralph build 100
ralph build 1
ralph build 3
ralph overview
```

At the time, `ralph build` used the Marrow PRD by default and kept stories in `open`, `in_progress`, and `done` states automatically.
`ralph build 100` meant "allow up to 100 iterations"; the historical loop stopped earlier if all stories finished.

## Notes

- `ralph prd` was pointed at Codex.
- `docs/root1/discription.md` remains the architecture source of truth and the checked-in PRD now expands it into dependency-ordered milestone checkpoints without prescribing a runner or automatic commits.
