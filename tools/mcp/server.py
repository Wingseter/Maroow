import asyncio
import json

from mcp.server import Server, NotificationOptions
from mcp.server.models import InitializationOptions
import mcp.types as types
from mcp.server.stdio import stdio_server

# Re-export for existing callers that import MarrowClient from server.
from marrow_client import MarrowClient

# Import modular components
from tools import inspection, editing
from resources import scene
from prompts import workflows

marrow = MarrowClient()
server = Server("marrow-control")

@server.list_resources()
async def handle_list_resources() -> list[types.Resource]:
    return await scene.list_resources()

@server.read_resource()
async def handle_read_resource(uri: str) -> str:
    return await scene.read_resource(marrow, uri)

@server.list_tools()
async def handle_list_tools() -> list[types.Tool]:
    return inspection.get_tools() + editing.get_tools()

@server.call_tool()
async def handle_call_tool(name: str, arguments: dict | None) -> list[types.TextContent]:
    # Pass all tools to the C++ agent dispatcher
    result = await marrow.send_command(name, arguments)
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]

@server.list_prompts()
async def handle_list_prompts() -> list[types.Prompt]:
    return workflows.get_prompts()

@server.get_prompt()
async def handle_get_prompt(name: str, arguments: dict | None) -> types.GetPromptResult:
    return types.GetPromptResult(
        description="Marrow Workflow",
        messages=workflows.get_prompt_message(name, arguments or {})
    )

async def main():
    try:
        async with stdio_server() as (read_stream, write_stream):
            await server.run(
                read_stream,
                write_stream,
                InitializationOptions(
                    server_name="marrow-control",
                    server_version="0.1.0",
                    capabilities=server.get_capabilities(
                        notification_options=NotificationOptions(),
                        experimental_capabilities={},
                    ),
                ),
            )
    finally:
        await marrow.aclose()

if __name__ == "__main__":
    asyncio.run(main())
