# Marrow-specific Ralph defaults.
# This workspace is now a git repo, so Ralph can commit per completed story.

PRD_PATH=".agents/tasks/prd-marrow-runtime.json"
PROGRESS_PATH=".ralph/progress.md"
GUARDRAILS_PATH=".ralph/guardrails.md"
ERRORS_LOG_PATH=".ralph/errors.log"
ACTIVITY_LOG_PATH=".ralph/activity.log"
TMP_DIR=".ralph/.tmp"
RUNS_DIR=".ralph/runs"
ACTIVITY_CMD=".agents/ralph/log-activity.sh"
AGENTS_PATH="AGENTS.md"
PROMPT_BUILD=".agents/ralph/PROMPT_build.md"

# Use the current Codex CLI flag names.
AGENT_CMD="codex exec --full-auto -"
PRD_AGENT_CMD="codex exec --full-auto {prompt}"

NO_COMMIT=false
MAX_ITERATIONS=25
STALE_SECONDS=120
