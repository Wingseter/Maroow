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
            description="Request editor approval to save the project",
            inputSchema={"type": "object", "properties": {}}
        ),
        types.Tool(
            name="export_runtime",
            description="Request editor approval to export runtime assets (.mskl, .matl, optional .mbin).",
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
            name="set_draw_order_keyframe",
            description="Create or replace a draw-order keyframe with the complete slot stack.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "time": {"type": "number"},
                    "slots": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": "Every skeleton slot exactly once, in draw order."
                    },
                    "merge": {"type": "boolean"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["animation", "time", "slots"]
            }
        ),
        types.Tool(
            name="remove_draw_order_keyframe",
            description="Remove a draw-order keyframe at an exact time.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "time": {"type": "number"}
                },
                "required": ["animation", "time"]
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
