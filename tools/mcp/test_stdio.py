"""MCP SDK integration against the real server process and a local agent peer.

Requires the existing tools/mcp virtualenv. No editor or user project is touched.
Authentication uses a literal placeholder, never a real credential.
"""

import asyncio
import json
from pathlib import Path
import sys
import unittest

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

from marrow_client import MarrowClient
from server import MarrowClient as ExportedMarrowClient
from tools import editing, inspection


class StdioIntegrationTests(unittest.IsolatedAsyncioTestCase):
    async def test_real_server_tools_round_trip_and_peer_eof_on_exit(self):
        self.assertIs(ExportedMarrowClient, MarrowClient)
        requests = []
        peer_errors = []
        peer_closed = asyncio.Event()
        handlers = set()
        writers = []

        async def accepted(reader, writer):
            task = asyncio.current_task()
            handlers.add(task)
            writers.append(writer)
            try:
                self.assertEqual(await reader.readline(), b"[REDACTED_SECRET]\n")
                writer.write(b'{"jsonrpc":"2.0","id":"auth","result":{"ok":true}}\n')
                await writer.drain()
                while line := await reader.readline():
                    request = json.loads(line)
                    requests.append(request)
                    response = {"jsonrpc": "2.0", "id": request["id"],
                                "result": {"ok": True, "op": request["op"]}}
                    writer.write((json.dumps(response) + "\n").encode())
                    await writer.drain()
                peer_closed.set()
            except asyncio.CancelledError:
                raise
            except Exception as error:
                peer_errors.append(error)
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except ConnectionError:
                    pass
                handlers.discard(task)

        listener = await asyncio.start_server(accepted, "127.0.0.1", 0)
        port = listener.sockets[0].getsockname()[1]
        parameters = StdioServerParameters(
            command=sys.executable,
            args=[str(Path(__file__).resolve().with_name("server.py"))],
            env={"MARROW_AGENT_PORT": str(port), "MARROW_AGENT_TOKEN": "[REDACTED_SECRET]"},
        )

        async def exercise():
            async with stdio_client(parameters) as (reader, writer):
                async with ClientSession(reader, writer) as session:
                    await session.initialize()
                    tools = await session.list_tools()
                    self.assertEqual(
                        {tool.name for tool in tools.tools},
                        {tool.name for tool in inspection.get_tools() + editing.get_tools()},
                    )
                    operations = ("bones.list", "slots.list")
                    results = await asyncio.gather(*(session.call_tool(op, {}) for op in operations))
                    for op, result in zip(operations, results):
                        self.assertFalse(result.isError)
                        self.assertEqual(len(result.content), 1)
                        self.assertEqual(json.loads(result.content[0].text), {"ok": True, "op": op})
            # The MCP process has exited. Its real TCP connection must be gone.
            await asyncio.wait_for(peer_closed.wait(), timeout=2.0)

        try:
            await asyncio.wait_for(exercise(), timeout=15.0)
            self.assertEqual(peer_errors, [])
            self.assertEqual(len(writers), 1)
            self.assertEqual({request["op"] for request in requests}, {"bones.list", "slots.list"})
            self.assertEqual(len(requests), 2)
            self.assertEqual(len({request["id"] for request in requests}), 2)
        finally:
            listener.close()
            await listener.wait_closed()
            active = list(handlers)
            for task in active:
                task.cancel()
            await asyncio.gather(*active, return_exceptions=True)
            for writer in writers:
                writer.close()
                try:
                    await writer.wait_closed()
                except ConnectionError:
                    pass


if __name__ == "__main__":
    unittest.main()
