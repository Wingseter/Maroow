import asyncio
import json
import os
import sys

from mcp.server import Server, NotificationOptions
from mcp.server.models import InitializationOptions
import mcp.types as types
from mcp.server.stdio import stdio_server

# Import modular components
from tools import inspection, editing
from resources import scene
from prompts import workflows

class MarrowClient:
    def __init__(self, host="127.0.0.1", port=9876, token=None):
        self.host = host
        self.port = int(os.environ.get("MARROW_AGENT_PORT", port))
        self.token = token if token is not None else os.environ.get("MARROW_AGENT_TOKEN", "")
        self.reader = None
        self.writer = None

    async def connect(self):
        if self.writer:
            return
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
        # Perform the optional handshake when a shared token is configured.
        if self.token:
            self.writer.write((self.token + "\n").encode())
            await self.writer.drain()
            ack = await self.reader.readline()
            if not ack or '"ok":true' not in ack.decode().replace(" ", ""):
                self.writer = None
                raise ConnectionError("Marrow agent token rejected")

    async def send_command(self, op, args=None):
        try:
            await self.connect()
            req = {
                "jsonrpc": "2.0",
                "id": "mcp-req",
                "op": op,
                "args": args or {}
            }
            self.writer.write((json.dumps(req) + "\n").encode())
            await self.writer.drain()
            
            line = await self.reader.readline()
            if not line:
                self.writer = None
                return {"ok": False, "message": "Connection closed by Marrow"}
                
            res = json.loads(line.decode())
            return res.get("result", {"ok": False, "message": "Malformed response"})
        except Exception as e:
            self.writer = None
            return {"ok": False, "message": f"Connection error: {str(e)}"}

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

if __name__ == "__main__":
    asyncio.run(main())
