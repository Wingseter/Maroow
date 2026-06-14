import mcp.types as types

def get_tools() -> list[types.Tool]:
    return [
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
        )
    ]
