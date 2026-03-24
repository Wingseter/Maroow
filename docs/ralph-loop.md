# Ralph Loop Setup For Marrow

This repository now includes a project-local Ralph configuration wired to Codex.

## Defaults

- Ralph config: `.agents/ralph/config.sh`
- PRD: `.agents/tasks/prd-marrow-runtime.json`
- State directory: `.ralph/`
- Agent runner: `codex exec --full-auto -`
- Commit mode: enabled because this folder now has a `.git/` directory

## Commands

```bash
ralph build 100
ralph build 1
ralph build 3
ralph overview
```

`ralph build` will use the Marrow PRD by default and will keep stories in `open`, `in_progress`, and `done` states automatically.
`ralph build 100` means "allow up to 100 iterations"; the loop will stop earlier if all stories are finished.

## Notes

- `ralph prd` is also pointed at Codex, but the checked-in PRD is already usable immediately.
- `docs/discription.md` remains the design source of truth and the checked-in PRD expands it into ordered implementation stories.
