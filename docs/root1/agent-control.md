# Maroow Agent Control (MCP) Documentation

## Overview

Maroow Agent Control allows AI agents to interact with the Maroow 2D animation editor via the Model Context Protocol (MCP). This enables automated rigging, animation, and project management using natural language.

## Security & Safety

- **Local Only**: The agent socket binds to `127.0.0.1` only.
- **Optional Handshake Token**: Launch the editor with `--agent-port <port> --agent-token <secret>`.
  When a token is set, the client's first line must be exactly that token; the
  server replies with an `authenticated` ack before accepting commands.
- **Undoable Actions**: Every mutation performed by an agent is pushed to the editor's undo stack.
- **Reviewed File Writes**: Agent `save`, `export_runtime`, and import/pack calls create
  editor review requests instead of writing files immediately.
- **Path Whitelisting**: File-write review requests are limited to the project directory,
  the resolved export directory, and temporary paths under `/tmp`.

## Implemented Scope

The current dispatcher implements inspection ops: `operations.list`,
`scene.describe`, `bones.list`, `animation.list`, `slots.list`, `skins.list`,
`attachments.list`, `constraints.list`, `timeline.describe`, `mesh.describe`,
and `project.diagnostics`.

The current dispatcher implements edit ops: `set_transform`,
`remove_transform_keyframe`, `set_draw_order_keyframe`,
`remove_draw_order_keyframe`, `set_event_keyframe`,
`remove_event_keyframe`, `set_deform_keyframe`, `remove_deform_keyframe`,
`set_vertex_weights`, `normalize_weights`, `set_slot_color_keyframe`,
`remove_slot_color_keyframe`, `set_attachment_keyframe`,
`remove_attachment_keyframe`, `edit_ik_constraint`, `edit_path_constraint`,
`edit_transform_constraint`, `edit_physics_constraint`, `undo`, and `redo`.

The current dispatcher implements management review ops: `save` and
`export_runtime`, plus import/pack dry-run and review ops:
`import.spine_json`, `import.spine_atlas`, `import.psd_layers`, and
`atlas.pack`. These return a review payload and require approval from the
editor Agent panel before files are written. Import/pack v1 validates and queues
local targets; actual importer execution remains available through the existing
CLI/smoke workflows.

The dispatcher implements validation/session ops: `export.preview`,
`runtime.validate`, `compare_runtime_export`, `agent.permissions.describe`,
`agent.pause`, `agent.resume`, and `agent.terminate`.

There is no separate `move_bone` op - bone motion is expressed as
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
- `export.preview`: Returns resolved export targets without writing files.
- `runtime.validate`: Builds runtime data and returns diagnostics.
- `compare_runtime_export`: Exports temporary JSON/binary files under `/tmp`
  and returns round-trip comparison metrics.
- `agent.permissions.describe`: Returns pause/termination/review state.

### Editing
- `set_transform`: Creates or updates a keyframe.
- `remove_transform_keyframe`: Removes a keyframe at a given time.
- `set_draw_order_keyframe`: Creates or replaces a full-slot-stack draw-order keyframe.
- `remove_draw_order_keyframe`: Removes a draw-order keyframe at a given time.
- `set_event_keyframe` / `remove_event_keyframe`: Edits event timelines.
- `set_deform_keyframe` / `remove_deform_keyframe`: Edits mesh deform timelines.
- `set_vertex_weights` / `normalize_weights`: Edits weighted mesh influences.
- `set_slot_color_keyframe` / `remove_slot_color_keyframe`: Edits slot RGBA timelines.
- `set_attachment_keyframe` / `remove_attachment_keyframe`: Edits attachment timelines.
- `edit_ik_constraint`: Modifies IK constraint properties.
- `edit_path_constraint`: Modifies path constraint properties.
- `edit_transform_constraint`: Modifies transform constraint properties.
- `edit_physics_constraint`: Modifies physics constraint properties.
- `undo` / `redo`: Navigates the edit history.

### Management
- `save`: Queues a reviewed `.marrow` project save request.
- `export_runtime`: Queues a reviewed `.mskl` / `.matl` / optional `.mbin` export request.
- `import.spine_json`: Validates or queues a reviewed Spine JSON import target.
- `import.spine_atlas`: Validates or queues a reviewed Spine atlas import target.
- `import.psd_layers`: Validates or queues reviewed PSD skeleton/atlas targets.
- `atlas.pack`: Validates or queues a reviewed atlas pack target.
- `agent.pause` / `agent.resume` / `agent.terminate`: Controls mutating operation intake.

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

## Validation

```bash
cmake --build build --target marrow_agent_dispatch_smoke
./build/marrow_agent_dispatch_smoke
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

For socket/MCP end-to-end validation, launch the editor with an agent port and
then run the MCP smoke client:

```bash
./build/marrow_editor_shell --agent-port 9876
python3 tools/mcp/test_client.py
```
