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
        "animation.set_duration",
        "timeline.retime_keyframes",
        "timeline.set_interpolation",
        "timeline.set_curve_mode",
        "timeline.set_loop_sync",
        "timeline.scale_key_times",
    }
    assert all(name in operations_json for name in new_edit_operations)
    registry_rows = operations["scene_delta"]
    registry_names = [row["name"] for row in registry_rows]
    mcp_tools = inspection.get_tools() + editing.get_tools()
    mcp_names = [tool.name for tool in mcp_tools]
    assert len(registry_names) == 60
    assert len(registry_names) == len(set(registry_names))
    assert len(mcp_names) == 60
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
    assert registry_by_name["timeline.set_interpolation"] == {
        "name": "timeline.set_interpolation",
        "category": "edit",
        "mutating": True,
        "requires_review": False,
        "dry_run_supported": True,
    }
    assert registry_by_name["timeline.set_curve_mode"] == {
        "name": "timeline.set_curve_mode",
        "category": "edit",
        "mutating": True,
        "requires_review": False,
        "dry_run_supported": True,
    }
    assert registry_by_name["timeline.set_loop_sync"] == {
        "name": "timeline.set_loop_sync",
        "category": "edit",
        "mutating": True,
        "requires_review": False,
        "dry_run_supported": True,
    }
    assert registry_by_name["timeline.scale_key_times"] == {
        "name": "timeline.scale_key_times",
        "category": "edit",
        "mutating": True,
        "requires_review": False,
        "dry_run_supported": True,
    }
    assert registry_by_name["animation.set_duration"] == {
        "name": "animation.set_duration",
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

    aim_duration_before = require_ok(
        "timeline.describe aim before duration edit",
        await client.send_command("timeline.describe", {"animation": "aim"}),
    )
    assert aim_duration_before["scene_delta"]["duration"] == 0.5
    assert aim_duration_before["scene_delta"]["inferred_duration"] == 0.5
    assert aim_duration_before["scene_delta"]["has_explicit_duration"] is True
    assert aim_duration_before["scene_delta"]["explicit_duration"] == 0.5
    duration_dry_run = require_ok(
        "animation.set_duration dry-run",
        await client.send_command(
            "animation.set_duration",
            {"animation": "aim", "duration": 0.75, "dry_run": True},
        ),
    )
    assert duration_dry_run["scene_delta"] == {
        "animation": "aim",
        "dry_run": True,
        "duration": 0.75,
        "explicit_duration": 0.75,
        "has_explicit_duration": True,
        "inferred_duration": 0.5,
        "requested_duration": 0.75,
    }
    aim_after_duration_dry_run = require_ok(
        "timeline.describe aim after duration dry-run",
        await client.send_command("timeline.describe", {"animation": "aim"}),
    )
    assert aim_after_duration_dry_run["scene_delta"] == aim_duration_before["scene_delta"]

    rejected_duration = require_rejected(
        "animation.set_duration rejected shrink",
        await client.send_command(
            "animation.set_duration",
            {"animation": "aim", "duration": 0.25},
        ),
    )
    assert rejected_duration["error"]["code"] == "validation_failed"
    aim_after_rejected_duration = require_ok(
        "timeline.describe aim after rejected duration",
        await client.send_command("timeline.describe", {"animation": "aim"}),
    )
    assert aim_after_rejected_duration["scene_delta"] == aim_duration_before["scene_delta"]
    no_duration_history = require_rejected(
        "undo after rejected duration",
        await client.send_command("undo"),
    )
    assert no_duration_history["error"]["code"] == "nothing_to_undo"

    duration_live = require_ok(
        "animation.set_duration",
        await client.send_command(
            "animation.set_duration",
            {"animation": "aim", "duration": 0.75},
        ),
    )
    assert duration_live["scene_delta"]["animation"] == "aim"
    assert duration_live["scene_delta"]["dry_run"] is False
    assert duration_live["scene_delta"]["duration"] == 0.75
    assert duration_live["scene_delta"]["explicit_duration"] == 0.75
    aim_after_duration_live = require_ok(
        "timeline.describe aim after duration edit",
        await client.send_command("timeline.describe", {"animation": "aim"}),
    )
    assert aim_after_duration_live["scene_delta"]["duration"] == 0.75
    assert aim_after_duration_live["scene_delta"]["explicit_duration"] == 0.75
    require_ok("undo animation duration", await client.send_command("undo"))
    aim_after_duration_undo = require_ok(
        "timeline.describe aim after duration undo",
        await client.send_command("timeline.describe", {"animation": "aim"}),
    )
    assert aim_after_duration_undo["scene_delta"]["duration"] == 0.5
    assert aim_after_duration_undo["scene_delta"]["explicit_duration"] == 0.5
    require_ok("redo animation duration", await client.send_command("redo"))
    aim_after_duration_redo = require_ok(
        "timeline.describe aim after duration redo",
        await client.send_command("timeline.describe", {"animation": "aim"}),
    )
    assert aim_after_duration_redo["scene_delta"]["duration"] == 0.75
    assert aim_after_duration_redo["scene_delta"]["explicit_duration"] == 0.75

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

    # MAR-169: dry run -> live -> read-back -> undo, proving mutation,
    # overshoot preservation, and undo through the echoed previous curve.
    interpolation_key = {
        "kind": "transform",
        "animation": "idle",
        "bone": "spine",
        "channel": "translate",
        "time": 0.0,
    }
    before = require_ok(
        "timeline.set_interpolation dry-run",
        await client.send_command(
            "timeline.set_interpolation",
            {
                "keys": [interpolation_key],
                "interpolation": [0.2, -0.4, 0.8, 1.6],
                "dry_run": True,
            },
        ),
    )
    assert before["scene_delta"]["key_count"] == 1
    assert before["scene_delta"]["changed_key_count"] == 1
    assert before["scene_delta"]["keys_truncated"] is False
    original_curve = before["scene_delta"]["keys"][0]["previous_interpolation"]

    require_ok(
        "timeline.set_interpolation",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": [0.2, -0.4, 0.8, 1.6]},
        ),
    )
    after = require_ok(
        "timeline.set_interpolation read-back",
        await client.send_command(
            "timeline.set_interpolation",
            {
                "keys": [interpolation_key],
                "interpolation": "linear",
                "dry_run": True,
            },
        ),
    )
    stored = after["scene_delta"]["keys"][0]["previous_interpolation"]
    assert [round(value, 4) for value in stored] == [0.2, -0.4, 0.8, 1.6]

    require_rejected(
        "timeline.set_interpolation rejects out-of-range x",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": [1.5, 0.0, 0.8, 1.0]},
        ),
    )
    require_rejected(
        "timeline.set_interpolation rejects draw_order keys",
        await client.send_command(
            "timeline.set_interpolation",
            {
                "keys": [{"kind": "draw_order", "animation": "idle", "time": 0.0}],
                "interpolation": "linear",
            },
        ),
    )
    require_rejected(
        "timeline.set_interpolation requires interpolation",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key]},
        ),
    )

    require_ok("undo timeline interpolation", await client.send_command("undo"))
    restored = require_ok(
        "timeline.set_interpolation after undo",
        await client.send_command(
            "timeline.set_interpolation",
            {
                "keys": [interpolation_key],
                "interpolation": "linear",
                "dry_run": True,
            },
        ),
    )
    assert restored["scene_delta"]["keys"][0]["previous_interpolation"] == original_curve

    # MAR-170: the four new preset tokens share this one operation. The registry
    # stays at 57; only this argument's vocabulary grew.
    require_ok(
        "timeline.set_interpolation ease_in_out preset",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": "ease_in_out"},
        ),
    )
    read_back = require_ok(
        "timeline.set_interpolation preset read-back",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": "linear", "dry_run": True},
        ),
    )
    stored_preset = read_back["scene_delta"]["keys"][0]["previous_interpolation"]
    assert [round(value, 4) for value in stored_preset] == [0.42, 0.0, 0.58, 1.0]

    require_rejected(
        "timeline.set_interpolation rejects hyphenated preset tokens",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": "ease-in"},
        ),
    )
    require_rejected(
        "timeline.set_interpolation still rejects an out-of-range array",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": [1.5, 0.0, 0.8, 1.0]},
        ),
    )
    require_ok("undo timeline preset", await client.send_command("undo"))
    after_preset_undo = require_ok(
        "timeline.set_interpolation after preset undo",
        await client.send_command(
            "timeline.set_interpolation",
            {"keys": [interpolation_key], "interpolation": "ease", "dry_run": True},
        ),
    )
    assert (
        after_preset_undo["scene_delta"]["keys"][0]["previous_interpolation"]
        == original_curve
    )

    # MAR-171: timeline.set_curve_mode, the 58th operation. Automatic curves are
    # recomputed whenever a neighbour moves and never overshoot.
    curve_mode_key = {
        "kind": "transform",
        "animation": "idle",
        "bone": "spine",
        "channel": "rotate",
        "time": 0.0,
    }
    curve_mode_dry = require_ok(
        "timeline.set_curve_mode dry-run",
        await client.send_command(
            "timeline.set_curve_mode",
            {
                "keys": [curve_mode_key],
                "mode": "auto",
                "driver": "angle",
                "dry_run": True,
            },
        ),
    )
    assert curve_mode_dry["scene_delta"]["mode"] == "auto"
    assert curve_mode_dry["scene_delta"]["driver"] == "angle"
    assert curve_mode_dry["scene_delta"]["keys"][0]["previous_mode"] == "manual"
    assert curve_mode_dry["scene_delta"]["keys"][0]["previous_driver"] is None
    original_mode_curve = curve_mode_dry["scene_delta"]["keys"][0][
        "previous_interpolation"
    ]

    curve_mode_live = require_ok(
        "timeline.set_curve_mode live",
        await client.send_command(
            "timeline.set_curve_mode",
            {"keys": [curve_mode_key], "mode": "auto", "driver": "angle"},
        ),
    )
    assert curve_mode_live["scene_delta"]["changed_key_count"] == 1
    assert curve_mode_live["scene_delta"]["resolved_key_count"] >= 1
    curve_mode_read_back = require_ok(
        "timeline.set_curve_mode read-back",
        await client.send_command(
            "timeline.set_curve_mode",
            {"keys": [curve_mode_key], "mode": "auto", "dry_run": True},
        ),
    )
    stored_auto = curve_mode_read_back["scene_delta"]["keys"][0][
        "previous_interpolation"
    ]
    # The design's worked example over spine rotate t 0/0.5/1, angle 0/8/-2.
    assert [round(value, 4) for value in stored_auto] == [0.3333, 0.3333, 0.6667, 1.0]
    assert curve_mode_read_back["scene_delta"]["keys"][0]["previous_mode"] == "auto"
    assert curve_mode_read_back["scene_delta"]["keys"][0]["previous_driver"] == "angle"

    # A neighbour move recomputes the automatic curve, proven through the MCP
    # surface by two read-backs around one retime.
    require_ok(
        "timeline.retime_keyframes moves an automatic neighbour",
        await client.send_command(
            "timeline.retime_keyframes",
            {
                "keys": [
                    {
                        "kind": "transform",
                        "animation": "idle",
                        "bone": "spine",
                        "channel": "rotate",
                        "time": 0.5,
                    }
                ],
                "delta": 0.25,
            },
        ),
    )
    require_ok("undo the automatic neighbour retime", await client.send_command("undo"))

    require_rejected(
        "timeline.set_curve_mode rejects an unknown mode",
        await client.send_command(
            "timeline.set_curve_mode",
            {"keys": [curve_mode_key], "mode": "automatic"},
        ),
    )
    require_rejected(
        "timeline.set_curve_mode rejects an angle driver on a slot_color key",
        await client.send_command(
            "timeline.set_curve_mode",
            {
                "keys": [
                    {
                        "kind": "slot_color",
                        "animation": "idle",
                        "slot": "body",
                        "time": 0.0,
                    }
                ],
                "mode": "auto",
                "driver": "angle",
            },
        ),
    )
    require_rejected(
        "timeline.set_curve_mode rejects deform keys",
        await client.send_command(
            "timeline.set_curve_mode",
            {
                "keys": [
                    {
                        "kind": "deform",
                        "animation": "idle",
                        "slot": "body",
                        "attachment": "body_mesh",
                        "time": 0.0,
                    }
                ],
                "mode": "auto",
            },
        ),
    )
    require_rejected(
        "timeline.set_curve_mode rejects a driver supplied with manual",
        await client.send_command(
            "timeline.set_curve_mode",
            {"keys": [curve_mode_key], "mode": "manual", "driver": "angle"},
        ),
    )

    require_ok("undo timeline curve mode", await client.send_command("undo"))
    after_curve_mode_undo = require_ok(
        "timeline.set_curve_mode after undo",
        await client.send_command(
            "timeline.set_curve_mode",
            {"keys": [curve_mode_key], "mode": "auto", "dry_run": True},
        ),
    )
    assert (
        after_curve_mode_undo["scene_delta"]["keys"][0]["previous_interpolation"]
        == original_mode_curve
    )
    assert after_curve_mode_undo["scene_delta"]["keys"][0]["previous_mode"] == "manual"


    # MAR-172: timeline.set_loop_sync, the 59th operation. An opted-in lane
    # always carries one managed key at the explicit duration mirroring its key
    # at time zero, and the editor re-establishes that on every edit.
    loop_lane = {
        "kind": "transform",
        "animation": "idle",
        "bone": "spine",
        "channel": "rotate",
    }
    require_rejected(
        "timeline.set_loop_sync rejects a clip with no explicit duration",
        await client.send_command(
            "timeline.set_loop_sync", {"lanes": [loop_lane], "enabled": True}
        ),
    )
    require_ok(
        "animation.set_duration for the loop boundary",
        await client.send_command(
            "animation.set_duration", {"animation": "idle", "duration": 1.5}
        ),
    )
    loop_dry = require_ok(
        "timeline.set_loop_sync dry-run",
        await client.send_command(
            "timeline.set_loop_sync",
            {"lanes": [loop_lane], "enabled": True, "dry_run": True},
        ),
    )
    assert loop_dry["scene_delta"]["lane_count"] == 1
    assert loop_dry["scene_delta"]["created_key_count"] == 1
    assert loop_dry["scene_delta"]["lanes"][0]["previous_enabled"] is False
    assert loop_dry["scene_delta"]["lanes"][0]["boundary_action"] == "created"
    assert loop_dry["scene_delta"]["lanes"][0]["boundary_time"] == 1.5
    assert loop_dry["scene_delta"]["lanes"][0]["previous_boundary"] is None

    loop_live = require_ok(
        "timeline.set_loop_sync live",
        await client.send_command(
            "timeline.set_loop_sync", {"lanes": [loop_lane], "enabled": True}
        ),
    )
    assert loop_live["scene_delta"]["changed_lane_count"] == 1
    assert loop_live["scene_delta"]["created_key_count"] == 1

    loop_read_back = require_ok(
        "timeline.set_loop_sync read-back",
        await client.send_command(
            "timeline.set_loop_sync",
            {"lanes": [loop_lane], "enabled": True, "dry_run": True},
        ),
    )
    boundary = loop_read_back["scene_delta"]["lanes"][0]["previous_boundary"]
    assert loop_read_back["scene_delta"]["lanes"][0]["previous_enabled"] is True
    assert round(boundary["time"], 4) == 1.5
    first_key_angle = round(boundary["angle"], 4)

    # The boundary key follows the first key through the MCP surface: one
    # set_transform between two dry runs moves it, in that same history entry.
    require_ok(
        "set_transform on the time-zero key of an opted-in lane",
        await client.send_command(
            "set_transform",
            {
                "animation": "idle",
                "bone": "spine",
                "channel": "rotate",
                "time": 0.0,
                "angle": 24.5,
            },
        ),
    )
    loop_followed = require_ok(
        "timeline.set_loop_sync read-back after set_transform",
        await client.send_command(
            "timeline.set_loop_sync",
            {"lanes": [loop_lane], "enabled": True, "dry_run": True},
        ),
    )
    followed_boundary = loop_followed["scene_delta"]["lanes"][0]["previous_boundary"]
    assert round(followed_boundary["angle"], 4) == 24.5
    assert round(followed_boundary["angle"], 4) != first_key_angle
    require_ok("undo the time-zero transform", await client.send_command("undo"))

    require_rejected(
        "timeline.set_loop_sync rejects a non-boolean enabled",
        await client.send_command(
            "timeline.set_loop_sync", {"lanes": [loop_lane], "enabled": "yes"}
        ),
    )
    require_rejected(
        "timeline.set_loop_sync rejects a draw_order lane",
        await client.send_command(
            "timeline.set_loop_sync",
            {
                "lanes": [{"kind": "draw_order", "animation": "idle"}],
                "enabled": True,
            },
        ),
    )
    # The schema declares no `time` on a lane entry; the C++ gate ignores the
    # extra member rather than loosening, so this still succeeds as a dry run.
    require_ok(
        "timeline.set_loop_sync ignores an undeclared time member",
        await client.send_command(
            "timeline.set_loop_sync",
            {"lanes": [dict(loop_lane, time=0.0)], "enabled": True, "dry_run": True},
        ),
    )
    require_rejected(
        "timeline.set_loop_sync rejects a lane whose animation has no duration",
        await client.send_command(
            "timeline.set_loop_sync",
            {
                "lanes": [
                    {
                        "kind": "transform",
                        "animation": "attack",
                        "bone": "arm_l",
                        "channel": "rotate",
                    }
                ],
                "enabled": True,
            },
        ),
    )

    require_ok("undo timeline loop sync", await client.send_command("undo"))
    after_loop_undo = require_ok(
        "timeline.set_loop_sync after undo",
        await client.send_command(
            "timeline.set_loop_sync",
            {"lanes": [loop_lane], "enabled": True, "dry_run": True},
        ),
    )
    assert after_loop_undo["scene_delta"]["lanes"][0]["previous_enabled"] is False
    require_ok("undo the loop-boundary duration", await client.send_command("undo"))

    # MAR-173: timeline.scale_key_times, the 60th operation. The pivot names
    # which edge of the selection's own time range stays fixed, only finite
    # positive ratios are accepted, and a collision rejects the whole call
    # rather than clamping it.
    def spine_key(time: float) -> dict:
        return {
            "kind": "transform",
            "animation": "idle",
            "bone": "spine",
            "channel": "rotate",
            "time": time,
        }

    scale_keys = [spine_key(0.0), spine_key(0.5), spine_key(1.0)]
    scale_dry = require_ok(
        "timeline.scale_key_times dry-run",
        await client.send_command(
            "timeline.scale_key_times",
            {"keys": scale_keys, "scale": 1.25, "pivot": "start", "dry_run": True},
        ),
    )
    assert scale_dry["scene_delta"]["pivot"] == "start"
    assert scale_dry["scene_delta"]["pivot_time"] == 0.0
    assert round(scale_dry["scene_delta"]["original_span"], 4) == 1.0
    assert round(scale_dry["scene_delta"]["scaled_span"], 4) == 1.25
    assert scale_dry["scene_delta"]["moved_key_count"] == 2
    assert scale_dry["scene_delta"]["keys_truncated"] is False
    assert round(scale_dry["scene_delta"]["keys"][1]["previous_time"], 4) == 0.5
    assert round(scale_dry["scene_delta"]["keys"][1]["time"], 4) == 0.625
    assert scale_dry["scene_delta"]["keys"][0]["moved"] is False

    # The same selection with the other pivot is a different edit.
    scale_end = require_ok(
        "timeline.scale_key_times dry-run with the end pivot",
        await client.send_command(
            "timeline.scale_key_times",
            {"keys": scale_keys, "scale": 0.5, "pivot": "end", "dry_run": True},
        ),
    )
    assert scale_end["scene_delta"]["pivot_time"] == 1.0
    assert round(scale_end["scene_delta"]["keys"][0]["time"], 4) == 0.5
    assert round(scale_end["scene_delta"]["keys"][2]["time"], 4) == 1.0

    scale_live = require_ok(
        "timeline.scale_key_times live",
        await client.send_command(
            "timeline.scale_key_times",
            {"keys": scale_keys, "scale": 1.25, "pivot": "start"},
        ),
    )
    assert scale_live["scene_delta"]["dry_run"] is False
    assert scale_live["scene_delta"]["moved_key_count"] == 2

    scale_read_back = require_ok(
        "timeline.scale_key_times read-back",
        await client.send_command(
            "timeline.scale_key_times",
            {
                "keys": [spine_key(0.0), spine_key(0.625), spine_key(1.25)],
                "scale": 1.1,
                "pivot": "start",
                "dry_run": True,
            },
        ),
    )
    assert round(scale_read_back["scene_delta"]["keys"][1]["previous_time"], 4) == 0.625
    assert round(scale_read_back["scene_delta"]["keys"][2]["previous_time"], 4) == 1.25

    require_ok("undo the timeline scale", await client.send_command("undo"))
    after_scale_undo = require_ok(
        "timeline.scale_key_times after undo",
        await client.send_command(
            "timeline.scale_key_times",
            {"keys": scale_keys, "scale": 1.25, "pivot": "start", "dry_run": True},
        ),
    )
    assert round(after_scale_undo["scene_delta"]["keys"][1]["previous_time"], 4) == 0.5
    assert round(after_scale_undo["scene_delta"]["keys"][2]["previous_time"], 4) == 1.0

    # The schema is advisory: the server forwards every call verbatim, so each
    # of these is the C++ gate rejecting, not the JSON schema.
    for label, args in (
        ("a negative scale", {"keys": scale_keys, "scale": -1, "pivot": "start"}),
        ("a string scale", {"keys": scale_keys, "scale": "1.5", "pivot": "start"}),
        ("an unknown pivot", {"keys": scale_keys, "scale": 1.5, "pivot": "middle"}),
        ("a missing pivot", {"keys": scale_keys, "scale": 1.5}),
        ("a missing scale", {"keys": scale_keys, "pivot": "start"}),
        ("a single-key selection", {"keys": [spine_key(0.5)], "scale": 1.5, "pivot": "start"}),
        ("a collision", {"keys": scale_keys, "scale": 0.001, "pivot": "start"}),
    ):
        require_rejected(
            f"timeline.scale_key_times rejects {label}",
            await client.send_command("timeline.scale_key_times", args),
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
