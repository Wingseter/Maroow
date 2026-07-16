import argparse
import asyncio
import json
import uuid

from server import MarrowClient
from tools import editing, inspection


def require_ok(label, result):
    if not result.get("ok"):
        raise AssertionError(f"{label} failed: {json.dumps(result, indent=2)}")
    return result


def require_rejected(label, result):
    if result.get("ok"):
        raise AssertionError(f"{label} unexpectedly succeeded: {json.dumps(result, indent=2)}")
    return result


async def test(parameter_only=False):
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
    registry_rows = operations["scene_delta"]
    registry_names = [row["name"] for row in registry_rows]
    mcp_tools = inspection.get_tools() + editing.get_tools()
    mcp_names = [tool.name for tool in mcp_tools]
    assert len(registry_names) == 55
    assert len(registry_names) == len(set(registry_names))
    assert len(mcp_names) == 55
    assert len(mcp_names) == len(set(mcp_names))
    assert set(registry_names) == set(mcp_names)

    registry_by_name = {row["name"]: row for row in registry_rows}
    assert registry_by_name["parameters.list"] == {
        "name": "parameters.list",
        "category": "inspection",
        "mutating": False,
        "requires_review": False,
        "dry_run_supported": False,
    }
    parameter_mutations = {
        "parameter.set",
        "deformer.create",
        "keyform.capture",
        "expression.create",
        "lip_sync.map",
    }
    for name in parameter_mutations:
        assert registry_by_name[name] == {
            "name": name,
            "category": "edit",
            "mutating": True,
            "requires_review": False,
            "dry_run_supported": True,
        }

    mcp_edit_tools = {tool.name for tool in editing.get_tools()}
    assert new_edit_operations <= mcp_edit_tools
    assert parameter_mutations <= mcp_edit_tools
    assert "parameters.list" in {tool.name for tool in inspection.get_tools()}

    scene = require_ok("scene.describe", await client.send_command("scene.describe"))
    assert scene["scene_delta"]["slot_count"] > 0

    slots = require_ok("slots.list", await client.send_command("slots.list"))
    slot_names = [slot["name"] for slot in slots["scene_delta"]]
    assert slot_names

    require_ok("bones.list", await client.send_command("bones.list"))
    require_ok("animation.list", await client.send_command("animation.list"))
    require_ok("skins.list", await client.send_command("skins.list"))
    require_ok("attachments.list", await client.send_command("attachments.list"))
    require_ok("constraints.list", await client.send_command("constraints.list"))

    parameters_before = require_ok(
        "parameters.list",
        await client.send_command("parameters.list"),
    )
    parameter_snapshot = json.dumps(
        parameters_before["scene_delta"], sort_keys=True, separators=(",", ":")
    )
    definitions = parameters_before["scene_delta"]["definitions"]
    parameter_definition = next(
        (
            definition
            for definition in definitions
            if definition["type"] == "continuous"
            and definition["max"] > definition["min"]
        ),
        None,
    )
    if parameter_only and parameter_definition is None:
        raise AssertionError(
            "--parameter-only requires a project with a ranged continuous parameter"
        )
    parameter_id = (
        parameter_definition["id"] if parameter_definition else "mcp.missing"
    )
    parameter_min = parameter_definition["min"] if parameter_definition else 0.0
    parameter_max = parameter_definition["max"] if parameter_definition else 1.0
    parameter_value = (parameter_min + parameter_max) * 0.5
    target_slot = slot_names[0]
    unique_suffix = uuid.uuid4().hex[:8]
    deformer_id = f"mcp.parameter.rotation.{unique_suffix}"
    expression_id = f"mcp.parameter.expression.{unique_suffix}"

    parameter_set_result = await client.send_command(
        "parameter.set",
        {"id": parameter_id, "value": parameter_value, "dry_run": True},
    )
    deformer_result = await client.send_command(
        "deformer.create",
        {
            "dry_run": True,
            "deformer": {
                "id": deformer_id,
                "name": "MCP Parameter Rotation",
                "kind": "rotation",
                "target_slots": [target_slot],
                "parameter_bindings": [
                    {"parameter": parameter_id, "axis": "angle"}
                ],
                "pivot": [0.0, 0.0],
                "influence": 0.5,
                "keyforms": [
                    {"value": parameter_min, "angle": -10.0},
                    {"value": parameter_max, "angle": 10.0},
                ],
            },
        },
    )
    capture_result = await client.send_command(
        "keyform.capture",
        {
            "deformer": deformer_id,
            "replace": False,
            "dry_run": True,
        },
    )
    expression_result = await client.send_command(
        "expression.create",
        {
            "dry_run": True,
            "expression": {
                "id": expression_id,
                "name": "MCP Parameter Expression",
                "targets": [{"parameter": parameter_id, "value": 0.25}],
                "duration": 0.1,
                "blend": "additive",
                "priority": 5,
                "reset_policy": "restore",
            },
        },
    )
    lip_result = await client.send_command(
        "lip_sync.map",
        {
            "dry_run": True,
            "mapping": {
                "source": "amplitude",
                "parameter": parameter_id,
                "scale": 1.0,
                "bias": 0.0,
                "attack": 0.02,
                "release": 0.08,
                "smoothing": 0.04,
            },
        },
    )
    if parameter_definition:
        require_ok("parameter.set dry-run", parameter_set_result)
        require_ok("deformer.create dry-run", deformer_result)
        require_rejected("keyform.capture uncommitted dry-run target", capture_result)
        require_ok("expression.create dry-run", expression_result)
        require_ok("lip_sync.map dry-run", lip_result)
    else:
        require_rejected("parameter.set missing parameter", parameter_set_result)
        require_rejected("deformer.create missing parameter", deformer_result)
        require_rejected("keyform.capture missing deformer", capture_result)
        require_rejected("expression.create missing parameter", expression_result)
        require_rejected("lip_sync.map missing parameter", lip_result)

    parameters_after = require_ok(
        "parameters.list after MCP dry-runs",
        await client.send_command("parameters.list"),
    )
    assert json.dumps(
        parameters_after["scene_delta"], sort_keys=True, separators=(",", ":")
    ) == parameter_snapshot

    if parameter_definition:
        require_ok(
            "parameter.set live socket E2E",
            await client.send_command(
                "parameter.set",
                {"id": parameter_id, "value": parameter_value},
            ),
        )
        before_deformer = require_ok(
            "parameters.list before deformer live socket E2E",
            await client.send_command("parameters.list"),
        )
        require_ok(
            "deformer.create live socket E2E",
            await client.send_command(
                "deformer.create",
                {
                    "deformer": {
                        "id": deformer_id,
                        "name": "MCP Parameter Rotation",
                        "kind": "rotation",
                        "target_slots": [target_slot],
                        "parameter_bindings": [
                            {"parameter": parameter_id, "axis": "angle"}
                        ],
                        "pivot": [0.0, 0.0],
                        "influence": 0.5,
                        "keyforms": [
                            {"value": parameter_min, "angle": -10.0},
                            {"value": parameter_max, "angle": 10.0},
                        ],
                    }
                },
            ),
        )
        after_deformer = require_ok(
            "parameters.list after deformer live socket E2E",
            await client.send_command("parameters.list"),
        )
        assert (
            after_deformer["scene_delta"]["runtime_revision"]
            > before_deformer["scene_delta"]["runtime_revision"]
        )

        before_capture_dry = json.dumps(
            after_deformer["scene_delta"], sort_keys=True, separators=(",", ":")
        )
        require_ok(
            "keyform.capture dry-run socket E2E",
            await client.send_command(
                "keyform.capture",
                {"deformer": deformer_id, "dry_run": True},
            ),
        )
        after_capture_dry = require_ok(
            "parameters.list after capture dry-run socket E2E",
            await client.send_command("parameters.list"),
        )
        assert json.dumps(
            after_capture_dry["scene_delta"], sort_keys=True, separators=(",", ":")
        ) == before_capture_dry
        require_ok(
            "keyform.capture live socket E2E",
            await client.send_command(
                "keyform.capture",
                {"deformer": deformer_id},
            ),
        )

        before_expression_dry = require_ok(
            "parameters.list before expression dry-run socket E2E",
            await client.send_command("parameters.list"),
        )
        expression_payload = {
            "id": expression_id,
            "name": "MCP Parameter Expression",
            "targets": [{"parameter": parameter_id, "value": 0.25}],
            "duration": 0.1,
            "blend": "additive",
            "priority": 5,
            "reset_policy": "restore",
        }
        require_ok(
            "expression.create dry-run socket E2E",
            await client.send_command(
                "expression.create",
                {"expression": expression_payload, "dry_run": True},
            ),
        )
        after_expression_dry = require_ok(
            "parameters.list after expression dry-run socket E2E",
            await client.send_command("parameters.list"),
        )
        assert json.dumps(
            after_expression_dry["scene_delta"],
            sort_keys=True,
            separators=(",", ":"),
        ) == json.dumps(
            before_expression_dry["scene_delta"],
            sort_keys=True,
            separators=(",", ":"),
        )
        require_ok(
            "expression.create live socket E2E",
            await client.send_command(
                "expression.create",
                {"expression": expression_payload},
            ),
        )

        before_lip_dry = require_ok(
            "parameters.list before lip dry-run socket E2E",
            await client.send_command("parameters.list"),
        )
        lip_payload = {
            "source": "amplitude",
            "parameter": parameter_id,
            "scale": 1.0,
            "bias": 0.0,
            "attack": 0.02,
            "release": 0.08,
            "smoothing": 0.04,
        }
        require_ok(
            "lip_sync.map dry-run socket E2E",
            await client.send_command(
                "lip_sync.map",
                {"mapping": lip_payload, "dry_run": True},
            ),
        )
        after_lip_dry = require_ok(
            "parameters.list after lip dry-run socket E2E",
            await client.send_command("parameters.list"),
        )
        assert json.dumps(
            after_lip_dry["scene_delta"], sort_keys=True, separators=(",", ":")
        ) == json.dumps(
            before_lip_dry["scene_delta"], sort_keys=True, separators=(",", ":")
        )
        require_ok(
            "lip_sync.map live socket E2E",
            await client.send_command("lip_sync.map", {"mapping": lip_payload}),
        )
        before_undo = require_ok(
            "parameters.list before parameter socket undo",
            await client.send_command("parameters.list"),
        )
        require_ok("parameter socket undo", await client.send_command("undo"))
        after_undo = require_ok(
            "parameters.list after parameter socket undo",
            await client.send_command("parameters.list"),
        )
        assert (
            after_undo["scene_delta"]["runtime_revision"]
            > before_undo["scene_delta"]["runtime_revision"]
        )
        require_ok(
            "parameter socket runtime.validate",
            await client.send_command("runtime.validate"),
        )
        print("mcp parameter test_client: PASSED")
        return

    assert "spark_fx" in slot_names

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
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--parameter-only",
        action="store_true",
        help="Run the successful parameter authoring socket E2E and stop.",
    )
    args = parser.parse_args()
    asyncio.run(test(parameter_only=args.parameter_only))
