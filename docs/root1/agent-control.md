# Maroow Agent Control (MCP) Documentation

## Overview

Maroow Agent Control allows AI agents to interact with the Maroow 2D animation editor via the Model Context Protocol (MCP). This enables automated rigging, animation, and project management using natural language.

## Security & Safety

- **Local Only**: The agent socket binds to `127.0.0.1` only.
- **Optional Handshake Token**: Launch the editor with `--agent-port <port> --agent-token <secret>`.
  When a token is set, the client's first line must be exactly that token; the
  server replies with an `authenticated` ack before accepting commands.
- **Undoable Actions**: Every mutation performed by an agent is pushed to the editor's undo stack.
- **Explicit Save**: Changes are not saved to disk unless the agent explicitly calls the `save` tool.
- **Path Whitelisting**: (Planned, not yet implemented) Import/Export operations
  will be restricted to the project directory and its subfolders.

## Implemented Scope

The current dispatcher implements: `scene.describe`, `bones.list`,
`animation.list`, `set_transform`, `remove_transform_keyframe`,
`edit_ik_constraint`, `undo`, `redo`, `save`, `export_runtime`. Mesh
deform/weight, draw-order, path/transform/physics constraints, and
PSD/Spine/atlas automation are planned for later phases and are **not** yet
available. There is no separate `move_bone` op — bone motion is expressed as
`set_transform` keyframes, matching the editor's data model.

## MCP Tools

### Inspection
- `scene.describe`: Returns project metadata and skeleton summary.
- `bones.list`: Lists all bones in the active skeleton.
- `animation.list`: Lists all animation names.

### Editing
- `set_transform`: Creates or updates a keyframe.
- `remove_transform_keyframe`: Removes a keyframe at a given time.
- `edit_ik_constraint`: Modifies IK constraint properties.
- `undo` / `redo`: Navigates the edit history.

### Management
- `save`: Persists the `.marrow` project.
- `export_runtime`: Exports `.mskl` and `.matl` assets for runtime use.

## Configuration

Add the following to your Claude Desktop configuration:

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

## Developer Guide

The system uses a **Single Shared Core** architecture. The `AgentCommandDispatcher` in C++ processes JSON commands, which are sent via a TCP socket from the Python MCP server.
