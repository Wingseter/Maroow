import mcp.types as types
import json

async def list_resources() -> list[types.Resource]:
    return [
        types.Resource(
            uri="marrow://scene",
            name="Current Scene",
            description="Read-only state of the current Marrow scene",
            mimeType="application/json"
        )
    ]

async def read_resource(marrow_client, uri: str) -> str:
    if uri == "marrow://scene":
        result = await marrow_client.send_command("scene.describe")
        return json.dumps(result, indent=2)
    raise ValueError(f"Unknown resource URI: {uri}")
