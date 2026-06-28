# Maroow Agent Control (MCP) Documentation

## Overview

Maroow Agent Control allows AI agents to interact with the Maroow 2D animation editor via the Model Context Protocol (MCP). This enables automated rigging, animation, and project management using natural language.

## Security & Safety

- **Local Only**: The agent socket binds to `127.0.0.1` only.
- **Optional Handshake Token**: Launch the editor with `--agent-port <port> --agent-token <secret>`.
  When a token is set, the client's first line must be exactly that token; the
  server replies with an `authenticated` ack before accepting commands.
- **Undoable Actions**: Every mutation performed by an agent is pushed to the editor's undo stack.
- **Reviewed File Writes**: Agent `save` and `export_runtime` calls create editor review
  requests instead of writing files immediately.
- **Path Whitelisting**: File-write review requests are limited to the project directory,
  the resolved export directory, and temporary paths under `/tmp`.

## Implemented Scope

The current dispatcher implements inspection ops: `operations.list`,
`scene.describe`, `bones.list`, `animation.list`, `slots.list`, `skins.list`,
`attachments.list`, `constraints.list`, `timeline.describe`, `mesh.describe`,
and `project.diagnostics`.

The current dispatcher implements edit ops: `set_transform`,
`remove_transform_keyframe`, `set_draw_order_keyframe`,
`remove_draw_order_keyframe`, `edit_ik_constraint`, `undo`, and `redo`.

The current dispatcher implements management review ops: `save` and
`export_runtime`. These return a review payload and require approval from the
editor Agent panel before files are written.

Mesh deform/weight writes, path/transform/physics constraint writes, and
PSD/Spine/atlas automation are planned for later phases and are **not** yet
available. There is no separate `move_bone` op - bone motion is expressed as
`set_transform` keyframes, matching the editor's data model.

## MCP Tools

### Inspection
- `scene.describe`: Returns project metadata and skeleton summary.
- `bones.list`: Lists all bones in the active skeleton.
- `animation.list`: Lists all animation names.
- `slots.list`: Lists slots, setup attachments, and bound bones.
- `skins.list`: Lists skins and attachment/constraint counts.
- `attachments.list`: Lists attachments, optionally filtered by skin and slot.
- `constraints.list`: Lists IK, path, transform, and physics constraints.
- `timeline.describe`: Summarizes authored timelines for one animation.
- `mesh.describe`: Summarizes one mesh attachment.
- `project.diagnostics`: Returns lightweight project diagnostics.

### Editing
- `set_transform`: Creates or updates a keyframe.
- `remove_transform_keyframe`: Removes a keyframe at a given time.
- `set_draw_order_keyframe`: Creates or replaces a full-slot-stack draw-order keyframe.
- `remove_draw_order_keyframe`: Removes a draw-order keyframe at a given time.
- `edit_ik_constraint`: Modifies IK constraint properties.
- `undo` / `redo`: Navigates the edit history.

### Management
- `save`: Queues a reviewed `.marrow` project save request.
- `export_runtime`: Queues a reviewed `.mskl` / `.matl` / optional `.mbin` export request.

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
