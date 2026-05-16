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
