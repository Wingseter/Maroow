import asyncio
import json

from server import MarrowClient


def require_ok(label, result):
    if not result.get("ok"):
        raise AssertionError(f"{label} failed: {json.dumps(result, indent=2)}")
    return result


async def test():
    client = MarrowClient()

    operations = require_ok(
        "operations.list",
        await client.send_command("operations.list")
    )
    assert "set_draw_order_keyframe" in json.dumps(operations)

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
        "set_transform",
        await client.send_command(
            "set_transform",
            {
                "animation": "idle",
                "bone": "arm_l",
                "channel": "rotate",
                "time": 0.25,
                "angle": 90.0,
            },
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

    require_ok("undo", await client.send_command("undo"))
    print("mcp test_client: PASSED")


if __name__ == "__main__":
    asyncio.run(test())
