import mcp.types as types


def _interpolation_schema() -> dict:
    return {
        "oneOf": [
            {"type": "string", "enum": ["linear", "stepped"]},
            {
                "type": "array",
                "items": {"type": "number"},
                "minItems": 4,
                "maxItems": 4,
            },
        ]
    }


def _color_schema() -> dict:
    return {
        "type": "object",
        "properties": {
            "r": {"type": "number"},
            "g": {"type": "number"},
            "b": {"type": "number"},
            "a": {"type": "number"},
        },
        "required": ["r", "g", "b", "a"],
    }


def _xy_schema() -> dict:
    return {
        "type": "object",
        "properties": {
            "x": {"type": "number"},
            "y": {"type": "number"},
        },
    }


def _influence_schema() -> dict:
    return {
        "type": "object",
        "properties": {
            "bone": {"type": "string"},
            "x": {"type": "number"},
            "y": {"type": "number"},
            "weight": {"type": "number"},
        },
        "required": ["bone", "x", "y", "weight"],
    }


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
                    "y": {"type": "number"},
                    "merge": {"type": "boolean"},
                    "dry_run": {"type": "boolean"}
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
                    "bend_positive": {"type": ["boolean", "null"]},
                    "bone_names": {"type": "array", "items": {"type": "string"}},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["name"]
            }
        ),
        types.Tool(
            name="edit_path_constraint",
            description="Partially edit a path constraint using project constraint-edit fields.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "slot": {"type": "string"},
                    "bone_names": {"type": "array", "items": {"type": "string"}},
                    "bones": {"type": "array", "items": {"type": "string"}},
                    "position": {"type": "number"},
                    "spacing": {"type": "number"},
                    "spacing_mode": {"type": "string", "enum": ["length", "percent"]},
                    "rotate_mix": {"type": "number"},
                    "translate_mix": {"type": "number"},
                    "merge": {"type": "boolean"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["name"]
            }
        ),
        types.Tool(
            name="edit_transform_constraint",
            description="Partially edit a transform constraint using project constraint-edit fields.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "source": {"type": "string"},
                    "bone_names": {"type": "array", "items": {"type": "string"}},
                    "bones": {"type": "array", "items": {"type": "string"}},
                    "rotate_mix": {"type": "number"},
                    "translate_mix": {"type": "number"},
                    "scale_mix": {"type": "number"},
                    "shear_mix": {"type": "number"},
                    "offset": {
                        "type": "object",
                        "properties": {
                            "rotation": {"type": "number"},
                            "x": {"type": "number"},
                            "y": {"type": "number"},
                            "scale_x": {"type": "number"},
                            "scale_y": {"type": "number"},
                            "shear_x": {"type": "number"},
                            "shear_y": {"type": "number"}
                        }
                    },
                    "merge": {"type": "boolean"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["name"]
            }
        ),
        types.Tool(
            name="edit_physics_constraint",
            description="Partially edit a physics constraint using project constraint-edit fields.",
            inputSchema={
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "bone_names": {"type": "array", "items": {"type": "string"}},
                    "bones": {"type": "array", "items": {"type": "string"}},
                    "step": {"type": "number"},
                    "x": {"type": "number"},
                    "y": {"type": "number"},
                    "rotate": {"type": "number"},
                    "scale_x": {"type": "number"},
                    "shear_x": {"type": "number"},
                    "limit": {"type": "number"},
                    "inertia": {"type": "number"},
                    "damping": {"type": "number"},
                    "strength": {"type": "number"},
                    "mass_inverse": {"type": "number"},
                    "gravity": _xy_schema(),
                    "wind": _xy_schema(),
                    "mix": {"type": "number"},
                    "merge": {"type": "boolean"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["name"]
            }
        ),
        types.Tool(
            name="set_event_keyframe",
            description="Create or replace an event keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "time": {"type": "number"},
                    "event": {"type": "string"},
                    "int": {"type": "number"},
                    "float": {"type": "number"},
                    "string": {"type": "string"},
                    "audio_path": {"type": "string"},
                    "volume": {"type": "number"},
                    "balance": {"type": "number"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["animation", "time", "event"]
            }
        ),
        types.Tool(
            name="remove_event_keyframe",
            description="Remove an event keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "time": {"type": "number"},
                    "event": {"type": "string"}
                },
                "required": ["animation", "time", "event"]
            }
        ),
        types.Tool(
            name="set_deform_keyframe",
            description="Create or replace a mesh deform keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "slot": {"type": "string"},
                    "attachment": {"type": "string"},
                    "time": {"type": "number"},
                    "offsets": {
                        "type": "array",
                        "items": {"type": "number"},
                        "maxItems": 65536
                    },
                    "interpolation": _interpolation_schema(),
                    "dry_run": {"type": "boolean"}
                },
                "required": ["animation", "slot", "attachment", "time", "offsets"]
            }
        ),
        types.Tool(
            name="remove_deform_keyframe",
            description="Remove a mesh deform keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "slot": {"type": "string"},
                    "attachment": {"type": "string"},
                    "time": {"type": "number"}
                },
                "required": ["animation", "slot", "attachment", "time"]
            }
        ),
        types.Tool(
            name="set_vertex_weights",
            description="Set weighted-mesh influences for selected vertices.",
            inputSchema={
                "type": "object",
                "properties": {
                    "skin": {"type": "string"},
                    "slot": {"type": "string"},
                    "attachment": {"type": "string"},
                    "vertices": {
                        "type": "array",
                        "items": {
                            "type": "object",
                            "properties": {
                                "index": {"type": "number"},
                                "influences": {
                                    "type": "array",
                                    "items": _influence_schema(),
                                    "minItems": 1,
                                    "maxItems": 4
                                }
                            },
                            "required": ["index", "influences"]
                        }
                    },
                    "normalize": {"type": "boolean"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["skin", "slot", "attachment", "vertices"]
            }
        ),
        types.Tool(
            name="normalize_weights",
            description="Normalize all weighted-mesh influences for an attachment.",
            inputSchema={
                "type": "object",
                "properties": {
                    "skin": {"type": "string"},
                    "slot": {"type": "string"},
                    "attachment": {"type": "string"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["skin", "slot", "attachment"]
            }
        ),
        types.Tool(
            name="set_slot_color_keyframe",
            description="Create or replace a slot RGBA color keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "slot": {"type": "string"},
                    "time": {"type": "number"},
                    "color": _color_schema(),
                    "interpolation": _interpolation_schema(),
                    "dry_run": {"type": "boolean"}
                },
                "required": ["animation", "slot", "time", "color"]
            }
        ),
        types.Tool(
            name="remove_slot_color_keyframe",
            description="Remove a slot color keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "slot": {"type": "string"},
                    "time": {"type": "number"}
                },
                "required": ["animation", "slot", "time"]
            }
        ),
        types.Tool(
            name="set_attachment_keyframe",
            description="Create or replace a slot attachment keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "slot": {"type": "string"},
                    "time": {"type": "number"},
                    "attachment": {"type": ["string", "null"]},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["animation", "slot", "time", "attachment"]
            }
        ),
        types.Tool(
            name="remove_attachment_keyframe",
            description="Remove a slot attachment keyframe.",
            inputSchema={
                "type": "object",
                "properties": {
                    "animation": {"type": "string"},
                    "slot": {"type": "string"},
                    "time": {"type": "number"}
                },
                "required": ["animation", "slot", "time"]
            }
        ),
        types.Tool(
            name="import.spine_json",
            description="Validate or queue a reviewed Spine JSON import.",
            inputSchema={
                "type": "object",
                "properties": {
                    "input": {"type": "string"},
                    "output": {"type": "string"},
                    "skeleton_output": {"type": "string"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["input"]
            }
        ),
        types.Tool(
            name="import.spine_atlas",
            description="Validate or queue a reviewed Spine atlas import.",
            inputSchema={
                "type": "object",
                "properties": {
                    "input": {"type": "string"},
                    "output": {"type": "string"},
                    "atlas_output": {"type": "string"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["input"]
            }
        ),
        types.Tool(
            name="import.psd_layers",
            description="Validate or queue a reviewed PSD layer import.",
            inputSchema={
                "type": "object",
                "properties": {
                    "input": {"type": "string"},
                    "output": {"type": "string"},
                    "skeleton_output": {"type": "string"},
                    "atlas_output": {"type": "string"},
                    "dry_run": {"type": "boolean"}
                },
                "required": ["input"]
            }
        ),
        types.Tool(
            name="atlas.pack",
            description="Validate or queue a reviewed atlas pack operation.",
            inputSchema={
                "type": "object",
                "properties": {
                    "output": {"type": "string"},
                    "atlas_output": {"type": "string"},
                    "dry_run": {"type": "boolean"}
                }
            }
        )
    ]
