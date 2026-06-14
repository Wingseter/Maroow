import asyncio
import json
import sys

from mcp.server import Server, NotificationOptions
from mcp.server.models import InitializationOptions
import mcp.types as types

from server import MarrowClient

async def test():
    client = MarrowClient()
    
    print("Testing scene.describe...")
    res = await client.send_command("scene.describe")
    print(json.dumps(res, indent=2))
    
    print("\nTesting bones.list...")
    res = await client.send_command("bones.list")
    print(json.dumps(res, indent=2))
    
    print("\nTesting animation.list...")
    res = await client.send_command("animation.list")
    print(json.dumps(res, indent=2))
    
    print("\nTesting set_transform...")
    res = await client.send_command("set_transform", {
        "animation": "idle",
        "bone": "arm_l",
        "channel": "rotate",
        "time": 0.25, # Different time
        "angle": 90.0 # Different angle
    })
    print(json.dumps(res, indent=2))

    print("\nTesting edit_ik_constraint...")
    res = await client.send_command("edit_ik_constraint", {
        "name": "editor_arm_reach",
        "mix": 0.75,
        "bend_positive": True
    })
    print(json.dumps(res, indent=2))

    print("\nTesting save...")
    res = await client.send_command("save")
    print(json.dumps(res, indent=2))
    
    print("\nTesting undo...")
    res = await client.send_command("undo")
    print(json.dumps(res, indent=2))

if __name__ == "__main__":
    asyncio.run(test())
