# MCP Transport Safety Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` and `superpowers:test-driven-development` for this checkpoint. This is the executable Phase 1 breakdown of the existing assessment, not a replacement product roadmap.

**Goal:** Prevent concurrent reads, stale responses after cancellation, and accidental replay of mutating agent commands.

**Architecture:** Keep one request in flight per MarrowClient connection. A single asyncio lock owns connect/authenticate/write/drain/read/validate and connection invalidation. Extract the transport into a standard-library-only module while retaining `from server import MarrowClient` compatibility. Use unique string IDs because the C++ response queue matches string IDs.

**Tech Stack:** Python asyncio/unittest, existing MCP SDK adapter, CMake/CTest.

**Spec:** `docs/refectoring_needs_checking.md`, Phase 1. Architecture and product authority remain `docs/root1/discription.md` and `.agents/tasks/prd-marrow-runtime.json`.

## Global Constraints

- Preserve existing operation names, arguments, result dictionaries, C++ protocol, file formats and transaction semantics.
- Never automatically replay a command after timeout, cancellation or transport/protocol failure; its execution outcome may be unknown.
- Propagate `asyncio.CancelledError`; a caller cancelled while waiting for the lock must not close another caller's active connection.
- Do not introduce runtime dependencies for transport regression tests.
- Preserve unrelated untracked `Untitled`, `build-sentinel/`, `build-specrev187/` and the user's assessment.
- Work in the selected feature checkout `feat/mar-168`; do not reset, stage, commit or switch branches.
- Phase 2 (shared frames), Phase 3 (shell lifecycle), Phase 4 (project split) and AGENTS archival remain subsequent checkpoints.

## Baseline

- `cmake --build build-sentinel -j 4`: PASS before edits.
- `ctest --test-dir build-sentinel --output-on-failure`: 24/24 PASS before edits.
- Existing client uses the constant `mcp-req`, unguarded shared streams, and drops writer references without closing on ordinary exceptions. Cancellation bypasses its `except Exception` block.

## Task 1 — Regression contracts and isolated transport

**Files:** create `tools/mcp/test_transport.py`, create `tools/mcp/marrow_client.py`, modify `tools/mcp/server.py`.

**Interfaces:** retain `MarrowClient(host="127.0.0.1", port=9876, token=None)` and `send_command(op, args=None)`; add keyword-only `request_timeout=30.0` and async `aclose()`.

- [x] Add local TCP tests against the original `server.MarrowClient` for concurrent calls, cancellation followed by a different command, response ID mismatch, connection loss/reconnect and mutation timeout without replay. Server-side received operations and connection identities are the oracles.
- [x] Run `tools/mcp/venv/bin/python tools/mcp/test_transport.py -v`; record the actual RED failures before changing production code.
- [x] Extract to `marrow_client.py`, re-export from `server.py`, then switch the focused test import to the dependency-free module.
- [x] Protect the complete exchange using one lock; bound connect/auth/exchange, use unique string IDs, strictly validate the JSON-RPC envelope and close/invalidate on cancellation/timeout/error.
- [x] Retain valid application-level `ok: false` results without retrying or dropping a healthy connection. Close the client in `server.main()`'s `finally` block.
- [x] Run `python3 tools/mcp/test_transport.py -v` until GREEN, without relaxing the behavioural assertions.

## Task 2 — Adversarial lifecycle coverage

**Files:** `tools/mcp/test_transport.py`, `tools/mcp/marrow_client.py`.

- [x] Add tests for cold concurrent connect, queued cancellation, explicit close/reconnect, authentication rejection and timeout, malformed/partial responses and unique IDs across independent clients.
- [x] Check peer EOF and captured writer closure, not merely cleared attributes. Avoid production transport mocks; use event synchronization and ephemeral loopback ports.
- [x] Run the suite under both system Python and the MCP virtualenv, with ResourceWarnings promoted to errors.

## Task 3 — Integration, documentation and verification

**Files:** `CMakeLists.txt`, `tools/mcp/README.md`, `AGENTS.md`, assessment and this checkpoint.

- [x] Register `marrow.mcp_transport` in CTest with a bounded test timeout and the discovered Python interpreter. Keep existing tests enabled.
- [x] Document timeout scope, cancellation propagation, no automatic retries, shutdown and standalone test commands. Add a short link/command at the AGENTS entrypoint instead of another long incident narrative.
- [x] Build and run the full CTest suite after integration. Verify MCP adapter import and Python syntax using the existing virtualenv.
- [x] Review the final diff, record actual commands/results and distinguish local TCP/C++ tests from any unexecuted live-editor MCP qualification.

## Completion evidence

Phase 1 implemented and locally verified on 2026-09-12. Phase 2–4 and AGENTS archival are not implemented by this checkpoint. No commit, push, reset or branch switch was performed.

### RED evidence

The original `server.MarrowClient` failed all five behavioural regression tests before production changes:

1. Concurrent calls: `readuntil() called while another coroutine is already waiting for incoming data`.
2. Cancellation: the subsequent `slots.list` returned `{"ok": true, "op": "bones.list"}`.
3. Connection loss: the captured old writer was not closing.
4. Mutation timeout: no client deadline; the outer test watchdog fired. This was reported as an explicit assertion failure before implementing the deadline.
5. Mismatched response ID: an unrelated successful result was accepted.

### Final verification

| Command | Result |
|---|---|
| `python3 -W error::ResourceWarning tools/mcp/test_transport.py -v` | 20/20 PASS |
| `tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_transport.py -v` | 20/20 PASS |
| `tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_stdio.py -v` | 1/1 PASS |
| `cmake --build build-sentinel -j 4` | PASS, exit 0 |
| `ctest --test-dir build-sentinel --output-on-failure` | 25/25 PASS, including `marrow.mcp_transport` |
| `tools/mcp/venv/bin/python -m py_compile tools/mcp/marrow_client.py tools/mcp/server.py tools/mcp/test_transport.py tools/mcp/test_stdio.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` | PASS, exit 0 |

`test_transport.py` includes thirteen malformed-frame subcases and five invalid-timeout subcases within its twenty test methods. There were no ResourceWarnings in either interpreter run. The original twenty-four CTest entries remain present. CMake selected Python 3.14.5 for the new entry; the standalone system `python3` run also passed.

An additional `tools/mcp/test_stdio.py` integration test uses the real MCP SDK and real `server.py` subprocess. It verifies the compatibility re-export, the complete tool-name set against the existing definitions, two simultaneous tool calls, authentication, unique wire IDs, one agent connection, and peer EOF when the process exits. Its agent endpoint is an isolated TCP fixture, not the C++ editor. It remains a separately documented SDK-dependent command rather than making the C++/transport CTest suite depend on the MCP virtualenv.

### Review and scope notes

- Public `connect()` and `aclose()` share request ownership; cancellation while queued cannot invalidate a different caller's active exchange.
- Stream references are detached before cleanup awaits. The captured writer is closed; bounded cleanup aborts on timeout/error or repeated cancellation.
- Valid application-level rejection dictionaries remain unchanged and keep a healthy connection. Protocol failures do not become successful results.
- UUID-based string IDs preserve C++ compatibility and avoid collisions between independent clients sharing the dispatcher's response queue.
- The existing response-line size limit was deliberately preserved, not increased.
- New source files are untracked until explicitly staged; `show_changes` reports tracked diffs only, so the new files were also reviewed from their written contents. No independent reviewer agent was used.
- Live C++ editor mutation E2E (`tools/mcp/test_client.py`), Windows execution, display/GPU qualification, and forced cleanup-timeout/repeated-cancellation fault injection were not run. The passing local tests do not claim those qualifications.
- The root AGENTS file was not archived or reduced; only two durable command/link lines were added, as required for a new test workflow.
