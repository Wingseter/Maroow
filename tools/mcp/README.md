# Maroow Agent Control (MCP)

This is a Model Context Protocol (MCP) server that allows AI agents (like Claude) to control the Maroow 2D animation editor.

## Features

- **Read-only Inspection**: List bones, animations, and describe the scene.
- **Animation Editing**: Set transform keyframes (Rotate, Translate, Scale, Shear).
- **Rigging**: Edit IK constraints.
- **History**: Undo/Redo support.
- **Persistence**: Save the project.

## Setup

1. Build Maroow with agent support:
   ```bash
   mkdir build && cd build
   cmake .. && make marrow_editor_shell -j8
   ```

2. Run Maroow with agent port:
   ```bash
   ./build/marrow_editor_shell --agent-port 9876
   ```

3. Configure Claude Desktop to use this MCP server. Add to `claude_desktop_config.json`:
   ```json
   {
     "mcpServers": {
       "marrow": {
         "command": "python3",
         "args": ["/path/to/Maroow/tools/mcp/server.py"]
       }
     }
   }
   ```

## Development

The server is split into:
- `tools/`: Individual tool definitions.
- `resources/`: Read-only scene state resources.
- `prompts/`: Common workflow templates.
- `marrow_client.py`: Standard-library-only agent TCP transport; `server.py` retains the `MarrowClient` import for compatibility.

## Transport contract

A client owns one connection and permits one in-flight exchange at a time. The same lock covers connection establishment, optional token authentication, request write/drain, response read/validation, and invalidation. Every request carries a unique string ID, including across reconnects and independent clients.

`MarrowClient(..., request_timeout=30.0)` sets a finite positive timeout in seconds. The deadline starts **after acquiring the request lock** and covers connect + authentication + write/drain + response read. Waiting callers do not consume this timeout; callers can apply their own outer deadline. Connection cleanup is separately bounded to one second before aborting the transport.

On timeout, connection loss, malformed/truncated responses, JSON-RPC errors or response ID mismatch, the client closes and discards both streams. A later explicit call establishes a fresh connection. A valid application result such as `{"ok": false, "message": "edit rejected"}` is returned unchanged and does not discard a healthy connection.

**Commands are never automatically replayed.** An edit might already have executed when its response is lost. A new request ID does not provide server-side deduplication. After an uncertain failure, inspect editor state before deciding whether to issue another edit.

Cancelling an in-flight call closes its connection and propagates `asyncio.CancelledError`. Cancelling a caller still waiting for the lock does not interrupt another caller's active exchange. `await client.aclose()` waits for the current exchange, closes the connection and is safe to repeat; a subsequent explicit call may reconnect. `server.main()` closes its client in a `finally` block.

The existing `MARROW_AGENT_PORT` and `MARROW_AGENT_TOKEN` environment settings remain supported. The token acknowledgement is parsed as JSON and must match the C++ agent's `jsonrpc: "2.0"`, `id: "auth"`, `result.ok: true` envelope. StreamReader's existing default response-line size limit is unchanged; oversized responses fail closed rather than being retried.

## Regression tests

From the repository root, transport tests need only Python 3.9+ (no MCP SDK and no editor process). They bind ephemeral loopback ports, isolate the agent environment, use real TCP streams, and check captured requests plus peer/transport closure:

```bash
python3 -W error::ResourceWarning tools/mcp/test_transport.py -v
```

CTest registers them as `marrow.mcp_transport`. `BUILD_TESTING=ON` requires CMake to find Python 3.9+; it never silently omits this test. Select an interpreter with `-DPython3_EXECUTABLE=/path/to/python` when configuring. Existing build directories discover the new registration during the next CMake configure/build:

```bash
cmake --build build-sentinel -j 4
ctest --test-dir build-sentinel -R 'marrow\.(mcp_transport|agent_socket_transport|agent_dispatch_smoke)' --output-on-failure
ctest --test-dir build-sentinel --output-on-failure
```

The separate SDK integration test launches the real `server.py` process over MCP stdio, checks its exported tool list and two concurrent tool calls against a local TCP peer, and verifies peer EOF on process exit. It uses the existing MCP virtualenv, not the standard-library-only CTest dependency set:

```bash
tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_stdio.py -v
```

These checks do not replace live-editor mutation E2E in `test_client.py`, Windows qualification, or display/GPU qualification. No user project is edited by either new test suite.
