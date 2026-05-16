import mcp.types as types

def get_tools() -> list[types.Tool]:
    return [
        types.Tool(
            name="undo",
            description="Undo the last action in Marrow editor",
            inputSchema={"type": "object", "properties": {}}
        ),
        types.Tool(
            name="redo",
            description="Redo the last undone action in Marrow editor",
            inputSchema={"type": "object", "properties": {}}
        ),
        types.Tool(
            name="save",
            description="Save the project",
            inputSchema={"type": "object", "properties": {}}
        ),
        types.Tool(
            name="export_runtime",
            description="Export the project to runtime assets (.mskl, .matl).",
            inputSchema={
                "type": "object",
                "properties": {
                    "binary": {
                        "type": "boolean",
                        "description": "Optional: Export as binary (.mbin) instead of JSON (.mskl)"
                    }
                }
            }
        ),
        types.Tool(
            name="set_transform",
            description="Set a keyframe for a bone's transform at a specific time.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "bone": {"type": "string"},
                    "channel": {"type": "string", "enum": ["rotate", "translate", "scale", "shear"]},
                    "time": {"type": "number"},
                    "angle": {"type": "number"},
                    "x": {"type": "number"},
                    "y": {"type": "number"}
                },
                "required": ["animation", "bone", "channel", "time"]
            }
        ),
        types.Tool(
            name="remove_transform_keyframe",
            description="Remove a transform keyframe for a bone at a specific time.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "bone": {"type": "string"},
                    "channel": {"type": "string", "enum": ["rotate", "translate", "scale", "shear"]},
                    "time": {"type": "number"}
                },
                "required": ["animation", "bone", "channel", "time"]
            }
        ),
        types.Tool(
            name="edit_ik_constraint",
            description="Edit properties of an IK constraint.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "target": {"type": ["string", "null"]},
                    "mix": {"type": ["number", "null"]},
                    "bend_positive": {"type": ["boolean", "null"]}
                },
                "required": ["name"]
            }
        )
    ]
