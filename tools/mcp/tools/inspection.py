import mcp.types as types

def get_tools() -> list[types.Tool]:
    return [
        types.Tool(
            name="operations.list",
            description="List supported C++ agent operations and metadata",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="scene.describe",
            description="Get information about the currently loaded scene",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="bones.list",
            description="List all bones in the current skeleton",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="animation.list",
            description="List all animations in the current skeleton",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="slots.list",
            description="List all slots in the current skeleton",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="skins.list",
            description="List skins and attachment counts",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="attachments.list",
            description="List attachments, optionally filtered by skin and slot",
            inputSchema={
                "type": "object",
                "properties": {
                    "skin": {"type": "string"},
                    "slot": {"type": "string"}
                }
            }
        ),
        types.Tool(
            name="constraints.list",
            description="List IK, path, transform, and physics constraints",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        types.Tool(
            name="timeline.describe",
            description="Describe timeline counts for one animation",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"}
                },
                "required": ["animation"]
            }
        ),
        types.Tool(
            name="mesh.describe",
            description="Describe one mesh attachment",
            inputSchema={
                "type": "object",
                "properties": {
                    "skin": {"type": "string"},
                    "slot": {"type": "string"},
                    "attachment": {"type": "string"}
                },
                "required": ["skin", "slot", "attachment"]
            }
        ),
        types.Tool(
            name="project.diagnostics",
            description="Return lightweight project diagnostics and review queue counts",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        )
    ]
