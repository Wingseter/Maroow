import asyncio
import json

from server import MarrowClient
from tools import editing


def require_ok(label, result):
    if not result.get("ok"):
        raise AssertionError(f"{label} failed: {json.dumps(result, indent=2)}")
    return result


def require_rejected(label, result):
    if result.get("ok"):
        raise AssertionError(f"{label} unexpectedly succeeded: {json.dumps(result, indent=2)}")
    return result


async def test():
    client = MarrowClient()

    operations = require_ok(
        "operations.list",
        await client.send_command("operations.list")
    )
    operations_json = json.dumps(operations)
    assert "set_slot_color_keyframe" in operations_json
    assert "dry_run_supported" in operations_json
    new_edit_operations = {
        "animation.create",
        "animation.duplicate",
        "animation.rename",
        "animation.delete",
        "timeline.retime_keyframes",
    }
    assert all(name in operations_json for name in new_edit_operations)
    mcp_edit_tools = {tool.name for tool in editing.get_tools()}
    assert new_edit_operations <= mcp_edit_tools

    scene = require_ok("scene.describe", await client.send_command("scene.describe"))
    assert scene["scene_delta"]["slot_count"] > 0

    slots = require_ok("slots.list", await client.send_command("slots.list"))
    slot_names = [slot["name"] for slot in slots["scene_delta"]]
    assert "spark_fx" in slot_names

    require_ok("bones.list", await client.send_command("bones.list"))
    require_ok("animation.list", await client.send_command("animation.list"))
    require_ok("skins.list", await client.send_command("skins.list"))
    require_ok("attachments.list", await client.send_command("attachments.list"))
    require_ok("constraints.list", await client.send_command("constraints.list"))
    require_ok(
        "timeline.describe",
        await client.send_command("timeline.describe", {"animation": "idle"})
    )
    require_ok(
        "mesh.describe",
        await client.send_command(
            "mesh.describe",
            {
                "skin": "mesh_base",
                "slot": "body",
                "attachment": "body_mesh",
            },
        )
    )
    require_ok("project.diagnostics", await client.send_command("project.diagnostics"))

    require_ok(
        "animation.create dry-run",
        await client.send_command(
            "animation.create",
            {"name": "mcp_empty", "dry_run": True},
        ),
    )
    require_ok(
        "animation.duplicate dry-run",
        await client.send_command(
            "animation.duplicate",
            {"source": "idle", "name": "mcp_idle_copy", "dry_run": True},
        ),
    )
    require_ok(
        "animation.rename dry-run",
        await client.send_command(
            "animation.rename",
            {"from": "attack", "to": "mcp_attack", "dry_run": True},
        ),
    )
    require_ok(
        "animation.delete dry-run",
        await client.send_command(
            "animation.delete",
            {"name": "attack", "dry_run": True},
        ),
    )
    require_ok(
        "timeline.retime_keyframes dry-run",
        await client.send_command(
            "timeline.retime_keyframes",
            {
                "delta": 0.05,
                "snap": False,
                "keys": [
                    {
                        "kind": "transform",
                        "animation": "idle",
                        "bone": "spine",
                        "channel": "translate",
                        "time": 0.5,
                    },
                    {
                        "kind": "slot_color",
                        "animation": "idle",
                        "slot": "body",
                        "time": 0.5,
                    },
                ],
                "dry_run": True,
            },
        ),
    )

    require_ok(
        "set_transform dry-run",
        await client.send_command(
            "set_transform",
            {
                "animation": "idle",
                "bone": "arm_l",
                "channel": "rotate",
                "time": 0.25,
                "angle": 95.0,
                "dry_run": True,
            },
        )
    )
    require_ok(
        "set_transform",
        await client.send_command(
            "set_transform",
            {
                "animation": "idle",
                "bone": "arm_l",
                "channel": "rotate",
                "time": 0.25,
                "angle": 95.0,
            },
        )
    )
    require_ok(
        "edit_path_constraint dry-run",
        await client.send_command(
            "edit_path_constraint",
            {
                "name": "editor_guide_follow",
                "position": 0.2,
                "dry_run": True,
            },
        )
    )
    require_ok(
        "edit_transform_constraint dry-run",
        await client.send_command(
            "edit_transform_constraint",
            {
                "name": "editor_transform_follow",
                "translate_mix": 0.5,
                "offset": {"x": -8},
                "dry_run": True,
            },
        )
    )
    require_ok(
        "edit_physics_constraint dry-run",
        await client.send_command(
            "edit_physics_constraint",
            {
                "name": "editor_ribbon_secondary",
                "mix": 0.8,
                "wind": {"x": 10},
                "dry_run": True,
            },
        )
    )
    require_ok(
        "set_event_keyframe",
        await client.send_command(
            "set_event_keyframe",
            {
                "animation": "idle",
                "time": 0.42,
                "event": "footstep",
                "int": 7,
                "float": 0.5,
                "string": "agent",
            },
        )
    )
    require_ok(
        "remove_event_keyframe",
        await client.send_command(
            "remove_event_keyframe",
            {"animation": "idle", "time": 0.42, "event": "footstep"},
        )
    )
    require_ok(
        "set_deform_keyframe",
        await client.send_command(
            "set_deform_keyframe",
            {
                "animation": "idle",
                "slot": "body",
                "attachment": "body_mesh",
                "time": 0.625,
                "offsets": [0, 0, 1, 0, 0, 1, 0, 0],
            },
        )
    )
    require_ok(
        "remove_deform_keyframe",
        await client.send_command(
            "remove_deform_keyframe",
            {
                "animation": "idle",
                "slot": "body",
                "attachment": "body_mesh",
                "time": 0.625,
            },
        )
    )
    require_ok(
        "set_vertex_weights dry-run",
        await client.send_command(
            "set_vertex_weights",
            {
                "skin": "mesh_base",
                "slot": "body",
                "attachment": "body_mesh",
                "vertices": [
                    {
                        "index": 1,
                        "influences": [
                            {"bone": "spine", "x": 60, "y": 0, "weight": 0.5},
                            {"bone": "arm_l", "x": 20, "y": 0, "weight": 0.5},
                        ],
                    }
                ],
                "dry_run": True,
            },
        )
    )
    require_ok(
        "set_slot_color_keyframe",
        await client.send_command(
            "set_slot_color_keyframe",
            {
                "animation": "idle",
                "slot": "body",
                "time": 0.625,
                "color": {"r": 0.5, "g": 0.75, "b": 1.0, "a": 0.8},
            },
        )
    )
    require_ok(
        "remove_slot_color_keyframe",
        await client.send_command(
            "remove_slot_color_keyframe",
            {"animation": "idle", "slot": "body", "time": 0.625},
        )
    )
    require_ok(
        "set_attachment_keyframe",
        await client.send_command(
            "set_attachment_keyframe",
            {
                "animation": "idle",
                "slot": "body",
                "time": 0.625,
                "attachment": "body",
            },
        )
    )
    require_ok(
        "remove_attachment_keyframe",
        await client.send_command(
            "remove_attachment_keyframe",
            {"animation": "idle", "slot": "body", "time": 0.625},
        )
    )

    rotated_slots = list(slot_names)
    rotated_slots[0], rotated_slots[1] = rotated_slots[1], rotated_slots[0]
    require_ok(
        "set_draw_order_keyframe",
        await client.send_command(
            "set_draw_order_keyframe",
            {
                "animation": "idle",
                "time": 0.75,
                "slots": rotated_slots,
            },
        )
    )
    require_ok(
        "remove_draw_order_keyframe",
        await client.send_command(
            "remove_draw_order_keyframe",
            {
                "animation": "idle",
                "time": 0.75,
            },
        )
    )

    export_review = require_ok(
        "export_runtime",
        await client.send_command("export_runtime", {"binary": True})
    )
    assert export_review["review"]["required"] is True
    require_ok("export.preview", await client.send_command("export.preview", {"binary": True}))
    require_ok("runtime.validate", await client.send_command("runtime.validate"))
    require_ok(
        "compare_runtime_export",
        await client.send_command("compare_runtime_export", {"binary": True})
    )
    require_ok(
        "import.spine_json dry-run",
        await client.send_command(
            "import.spine_json",
            {
                "input": "assets/fixtures/spine_import_sample.json",
                "output": "/tmp/agent_spine_import_sample.mskl",
            },
        )
    )
    import_review = require_ok(
        "import.spine_json review",
        await client.send_command(
            "import.spine_json",
            {
                "input": "assets/fixtures/spine_import_sample.json",
                "output": "/tmp/agent_spine_import_sample.mskl",
                "dry_run": False,
            },
        )
    )
    assert import_review["review"]["kind"] == "import_or_pack"
    require_ok("agent.permissions.describe", await client.send_command("agent.permissions.describe"))
    require_ok("agent.pause", await client.send_command("agent.pause"))
    require_rejected(
        "paused mutation blocked",
        await client.send_command(
            "set_transform",
            {
                "animation": "idle",
                "bone": "spine",
                "channel": "rotate",
                "time": 0.8,
                "angle": 5,
            },
        )
    )
    require_ok("agent.resume", await client.send_command("agent.resume"))

    require_ok("undo", await client.send_command("undo"))
    print("mcp test_client: PASSED")


if __name__ == "__main__":
    asyncio.run(test())
