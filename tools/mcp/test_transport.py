"""Marrow transport contracts over real, isolated loopback TCP connections."""

import asyncio
import json
import os
import unittest
from unittest.mock import patch

from marrow_client import MarrowClient


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.server = None
        self.clients = []
        self.client_writers = []
        self.peer_writers = []
        self.handlers = set()
        self.tasks = []
        self.connections = 0
        self.requests = []
        self.peer_errors = []

    async def asyncTearDown(self):
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        # Retain references even when the old client discards an unclosed writer.
        for client in self.clients:
            if client.writer is not None:
                self.client_writers.append(client.writer)
        for writer in self.client_writers + self.peer_writers:
            writer.close()
        for writer in self.client_writers + self.peer_writers:
            try:
                await writer.wait_closed()
            except ConnectionError:
                pass
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
        handlers = list(self.handlers)
        for task in handlers:
            task.cancel()
        await asyncio.gather(*handlers, return_exceptions=True)
        self.assertEqual(self.peer_errors, [], "unexpected exception in test TCP peer")

    def spawn(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        return task

    async def bounded(self, awaitable):
        return await asyncio.wait_for(awaitable, timeout=2.0)

    async def start_client(self, handler, *, connect=True, token=""):
        async def accepted(reader, writer):
            task = asyncio.current_task()
            self.handlers.add(task)
            self.peer_writers.append(writer)
            self.connections += 1
            connection = self.connections
            try:
                await handler(reader, writer, connection)
            except (ConnectionError, asyncio.CancelledError):
                pass
            except Exception as error:
                self.peer_errors.append(error)
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except ConnectionError:
                    pass
                self.handlers.discard(task)

        self.server = await asyncio.start_server(accepted, "127.0.0.1", 0)
        port = self.server.sockets[0].getsockname()[1]
        # Do not accidentally use an editor configured in the developer's shell.
        environment = patch.dict(os.environ, {"MARROW_AGENT_PORT": str(port)})
        environment.start()
        self.addCleanup(environment.stop)
        client = MarrowClient(port=port, token=token)
        self.clients.append(client)
        if connect:
            await self.bounded(client.connect())
            self.client_writers.append(client.writer)
        return client

    async def receive(self, reader, connection):
        line = await reader.readline()
        if not line:
            return None
        request = json.loads(line)
        self.requests.append((connection, request))
        return request

    async def reply(self, writer, request, **result):
        response = {"jsonrpc": "2.0", "id": request["id"],
                    "result": {"ok": True, "op": request["op"], **result}}
        writer.write((json.dumps(response) + "\n").encode())
        await writer.drain()

    async def echo(self, reader, writer, connection):
        while (request := await self.receive(reader, connection)) is not None:
            await self.reply(writer, request)

    async def test_concurrent_requests_are_serialized_and_correlated(self):
        received = asyncio.Event()
        release = asyncio.Event()
        attempted = asyncio.Event()

        async def peer(reader, writer, connection):
            while (request := await self.receive(reader, connection)) is not None:
                if request["op"] == "bones.list":
                    received.set()
                    await release.wait()
                await self.reply(writer, request)

        client = await self.start_client(peer)
        first = self.spawn(client.send_command("bones.list"))
        await self.bounded(received.wait())

        async def second_call():
            attempted.set()
            return await client.send_command("slots.list")

        second = self.spawn(second_call())
        await self.bounded(attempted.wait())
        release.set()
        results = await self.bounded(asyncio.gather(first, second))
        self.assertEqual(results, [{"ok": True, "op": "bones.list"},
                                   {"ok": True, "op": "slots.list"}])
        self.assertEqual(self.connections, 1)
        self.assertEqual(len({request["id"] for _, request in self.requests}), 2)

    async def test_cancellation_cannot_deliver_a_stale_response_to_next_request(self):
        received = asyncio.Event()
        release = asyncio.Event()

        async def peer(reader, writer, connection):
            while (request := await self.receive(reader, connection)) is not None:
                if request["op"] == "bones.list":
                    received.set()
                    await release.wait()
                await self.reply(writer, request)

        client = await self.start_client(peer)
        old_writer = client.writer
        first = self.spawn(client.send_command("bones.list"))
        await self.bounded(received.wait())
        first.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await first
        release.set()
        result = await self.bounded(client.send_command("slots.list"))
        self.assertEqual(result, {"ok": True, "op": "slots.list"})
        self.assertTrue(old_writer.is_closing())
        self.assertEqual(self.connections, 2)

    async def test_response_id_mismatch_is_rejected_and_connection_closed(self):
        async def peer(reader, writer, connection):
            while (request := await self.receive(reader, connection)) is not None:
                if connection == 1:
                    writer.write(b'{"jsonrpc":"2.0","id":"wrong","result":{"ok":true}}\n')
                    await writer.drain()
                else:
                    await self.reply(writer, request)

        client = await self.start_client(peer)
        old_writer = client.writer
        result = await self.bounded(client.send_command("bones.list"))
        self.assertFalse(result["ok"], result)
        self.assertIn("ID", result["message"])
        self.assertTrue(old_writer.is_closing())
        self.assertIsNone(client.reader)
        self.assertIsNone(client.writer)
        self.assertEqual(await self.bounded(client.send_command("slots.list")),
                         {"ok": True, "op": "slots.list"})
        self.assertEqual(self.connections, 2)

    async def test_connection_loss_is_not_replayed_and_next_call_reconnects(self):
        async def peer(reader, writer, connection):
            if connection == 1:
                await self.receive(reader, connection)
                writer.transport.abort()
                return
            await self.echo(reader, writer, connection)

        client = await self.start_client(peer)
        old_writer = client.writer
        result = await self.bounded(client.send_command("animation.create", {"name": "once"}))
        self.assertFalse(result["ok"], result)
        self.assertTrue(old_writer.is_closing())
        self.assertEqual(await self.bounded(client.send_command("slots.list")),
                         {"ok": True, "op": "slots.list"})
        self.assertEqual([request["op"] for _, request in self.requests],
                         ["animation.create", "slots.list"])
        self.assertEqual(self.connections, 2)

    async def test_mutating_timeout_closes_connection_without_automatic_retry(self):
        peer_closed = asyncio.Event()

        async def peer(reader, writer, connection):
            if connection == 1:
                await self.receive(reader, connection)
                if await reader.read() == b"":
                    peer_closed.set()
                return
            await self.echo(reader, writer, connection)

        client = await self.start_client(peer)
        client.request_timeout = 0.05
        old_writer = client.writer
        try:
            result = await self.bounded(client.send_command("animation.create", {"name": "once"}))
        except asyncio.TimeoutError:
            self.fail("client did not enforce its request timeout; outer test watchdog fired")
        self.assertFalse(result["ok"], result)
        self.assertIn("timed out", result["message"])
        self.assertTrue(old_writer.is_closing())
        await self.bounded(peer_closed.wait())
        self.assertEqual(await self.bounded(client.send_command("slots.list")),
                         {"ok": True, "op": "slots.list"})
        self.assertEqual([request["op"] for _, request in self.requests],
                         ["animation.create", "slots.list"])

    async def test_cold_concurrent_calls_open_only_one_connection(self):
        client = await self.start_client(self.echo, connect=False)
        names = [f"inspection.{index}" for index in range(8)]
        results = await self.bounded(asyncio.gather(*(client.send_command(name) for name in names)))
        self.assertEqual(results, [{"ok": True, "op": name} for name in names])
        self.assertEqual(self.connections, 1)
        self.assertEqual(len({request["id"] for _, request in self.requests}), len(names))

    async def test_cancelling_a_queued_caller_preserves_the_active_exchange(self):
        received = asyncio.Event()
        release = asyncio.Event()
        attempted = asyncio.Event()

        async def peer(reader, writer, connection):
            while (request := await self.receive(reader, connection)) is not None:
                if request["op"] == "bones.list":
                    received.set()
                    await release.wait()
                await self.reply(writer, request)

        client = await self.start_client(peer)
        writer = client.writer
        first = self.spawn(client.send_command("bones.list"))
        await self.bounded(received.wait())

        async def queued_call():
            attempted.set()
            return await client.send_command("animation.create")

        queued = self.spawn(queued_call())
        await self.bounded(attempted.wait())
        queued.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await queued
        self.assertIs(client.writer, writer)
        self.assertFalse(writer.is_closing())
        release.set()
        self.assertEqual(await self.bounded(first), {"ok": True, "op": "bones.list"})
        self.assertEqual(await self.bounded(client.send_command("slots.list")),
                         {"ok": True, "op": "slots.list"})
        self.assertEqual([request["op"] for _, request in self.requests],
                         ["bones.list", "slots.list"])
        self.assertEqual(self.connections, 1)

    async def test_public_connect_serializes_authentication_with_commands(self):
        received = asyncio.Event()
        release = asyncio.Event()
        attempted = asyncio.Event()
        tokens = []

        async def peer(reader, writer, connection):
            tokens.append(await reader.readline())
            received.set()
            await release.wait()
            writer.write(b'{"jsonrpc":"2.0","id":"auth","result":{"ok":true}}\n')
            await writer.drain()
            await self.echo(reader, writer, connection)

        client = await self.start_client(peer, connect=False, token="[REDACTED_SECRET]")
        connecting = self.spawn(client.connect())
        await self.bounded(received.wait())

        async def command():
            attempted.set()
            return await client.send_command("bones.list")

        pending = self.spawn(command())
        await self.bounded(attempted.wait())
        release.set()
        self.assertEqual(await self.bounded(pending), {"ok": True, "op": "bones.list"})
        await self.bounded(connecting)
        self.assertEqual(tokens, [b"[REDACTED_SECRET]\n"])
        self.assertEqual(self.connections, 1)

    async def test_rejected_authentication_sends_no_command_and_reconnects(self):
        closed = asyncio.Event()
        tokens = []

        async def peer(reader, writer, connection):
            tokens.append(await reader.readline())
            if connection == 1:
                writer.write(b'{"jsonrpc":"2.0","id":null,"error":{"code":-32001,"message":"Unauthorized"}}\n')
                await writer.drain()
                self.assertEqual(await reader.read(), b"")
                closed.set()
                return
            writer.write(b'{"jsonrpc":"2.0","id":"auth","result":{"ok":true}}\n')
            await writer.drain()
            await self.echo(reader, writer, connection)

        client = await self.start_client(peer, connect=False, token="[REDACTED_SECRET]")
        result = await self.bounded(client.send_command("animation.create"))
        self.assertFalse(result["ok"])
        self.assertIn("token rejected", result["message"])
        await self.bounded(closed.wait())
        self.assertIsNone(client.reader)
        self.assertIsNone(client.writer)
        self.assertEqual(self.requests, [])
        self.assertEqual(await self.bounded(client.send_command("slots.list")),
                         {"ok": True, "op": "slots.list"})
        self.assertEqual(tokens, [b"[REDACTED_SECRET]\n", b"[REDACTED_SECRET]\n"])
        self.assertEqual([request["op"] for _, request in self.requests], ["slots.list"])

    async def test_unrelated_ok_true_does_not_authenticate_a_rejected_result(self):
        closed = asyncio.Event()

        async def peer(reader, writer, connection):
            await reader.readline()
            writer.write(b'{"jsonrpc":"2.0","id":"auth","result":{"ok":false},"unrelated":{"ok":true}}\n')
            await writer.drain()
            self.assertEqual(await reader.read(), b"")
            closed.set()

        client = await self.start_client(peer, connect=False, token="[REDACTED_SECRET]")
        self.assertFalse((await self.bounded(client.send_command("animation.create")))["ok"])
        await self.bounded(closed.wait())
        self.assertIsNone(client.writer)

    async def test_cancellation_during_handshake_closes_the_peer(self):
        received = asyncio.Event()
        closed = asyncio.Event()

        async def peer(reader, writer, connection):
            self.assertEqual(await reader.readline(), b"[REDACTED_SECRET]\n")
            received.set()
            self.assertEqual(await reader.read(), b"")
            closed.set()

        client = await self.start_client(peer, connect=False, token="[REDACTED_SECRET]")
        task = self.spawn(client.send_command("animation.create"))
        await self.bounded(received.wait())
        writer = client.writer
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        await self.bounded(closed.wait())
        self.assertTrue(writer.is_closing())
        self.assertIsNone(client.reader)
        self.assertIsNone(client.writer)
        self.assertEqual(self.requests, [])

    async def test_public_connect_timeout_closes_the_handshake_connection(self):
        closed = asyncio.Event()

        async def peer(reader, writer, connection):
            self.assertEqual(await reader.readline(), b"[REDACTED_SECRET]\n")
            self.assertEqual(await reader.read(), b"")
            closed.set()

        client = await self.start_client(peer, connect=False, token="[REDACTED_SECRET]")
        client.request_timeout = 0.05
        with self.assertRaises(asyncio.TimeoutError):
            await self.bounded(client.connect())
        await self.bounded(closed.wait())
        self.assertIsNone(client.reader)
        self.assertIsNone(client.writer)
        self.assertEqual(self.connections, 1)

    async def test_handshake_timeout_never_sends_the_mutating_command(self):
        closed = asyncio.Event()

        async def peer(reader, writer, connection):
            self.assertEqual(await reader.readline(), b"[REDACTED_SECRET]\n")
            self.assertEqual(await reader.read(), b"")
            closed.set()

        client = await self.start_client(peer, connect=False, token="[REDACTED_SECRET]")
        client.request_timeout = 0.05
        result = await self.bounded(client.send_command("animation.create"))
        self.assertFalse(result["ok"])
        self.assertIn("timed out", result["message"])
        await self.bounded(closed.wait())
        self.assertEqual(self.connections, 1)
        self.assertEqual(self.requests, [])

    async def test_aclose_is_idempotent_and_allows_fresh_reconnect(self):
        client = await self.start_client(self.echo)
        await self.bounded(client.send_command("bones.list"))
        writer = client.writer
        await self.bounded(client.aclose())
        await self.bounded(client.aclose())
        self.assertTrue(writer.is_closing())
        self.assertIsNone(client.reader)
        self.assertIsNone(client.writer)
        self.assertEqual(await self.bounded(client.send_command("slots.list")),
                         {"ok": True, "op": "slots.list"})
        self.assertEqual(self.connections, 2)
        self.assertEqual(len({request["id"] for _, request in self.requests}), 2)

    async def test_close_waits_for_inflight_response_instead_of_interrupting_it(self):
        received = asyncio.Event()
        release = asyncio.Event()
        attempted = asyncio.Event()

        async def peer(reader, writer, connection):
            request = await self.receive(reader, connection)
            received.set()
            await release.wait()
            await self.reply(writer, request)
            self.assertEqual(await reader.read(), b"")

        client = await self.start_client(peer)
        first = self.spawn(client.send_command("bones.list"))
        await self.bounded(received.wait())

        async def close():
            attempted.set()
            await client.aclose()

        closing = self.spawn(close())
        await self.bounded(attempted.wait())
        self.assertFalse(closing.done())
        release.set()
        self.assertEqual(await self.bounded(first), {"ok": True, "op": "bones.list"})
        await self.bounded(closing)
        self.assertIsNone(client.writer)

    async def test_application_rejection_preserves_result_and_healthy_connection(self):
        rejection = {"ok": False, "message": "edit rejected", "issues": ["missing bone"]}

        async def peer(reader, writer, connection):
            request = await self.receive(reader, connection)
            writer.write((json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": rejection}) + "\n").encode())
            await writer.drain()
            await self.echo(reader, writer, connection)

        client = await self.start_client(peer)
        writer = client.writer
        self.assertEqual(await self.bounded(client.send_command("animation.create")), rejection)
        self.assertIs(client.writer, writer)
        self.assertFalse(writer.is_closing())
        self.assertEqual(await self.bounded(client.send_command("bones.list")),
                         {"ok": True, "op": "bones.list"})
        self.assertEqual(self.connections, 1)

    async def test_invalid_frames_are_rejected_before_clean_reconnect(self):
        def frame(request, **changes):
            response = {"jsonrpc": "2.0", "id": request["id"], "result": {"ok": True}}
            response.update(changes)
            return (json.dumps(response) + "\n").encode()

        invalid = [
            ("invalid JSON", lambda request: b"not json\n"),
            ("invalid UTF8", lambda request: b"\xff\n"),
            ("non-object root", lambda request: b"[]\n"),
            ("wrong version", lambda request: frame(request, jsonrpc="1.0")),
            ("missing id", lambda request: b'{"jsonrpc":"2.0","result":{"ok":true}}\n'),
            ("null id", lambda request: frame(request, id=None)),
            ("missing result", lambda request: (json.dumps({"jsonrpc": "2.0", "id": request["id"]}) + "\n").encode()),
            ("non-object result", lambda request: frame(request, result=[])),
            ("missing ok", lambda request: frame(request, result={})),
            ("non-boolean ok", lambda request: frame(request, result={"ok": 1})),
            ("error envelope", lambda request: frame(request, error={"code": -32700})),
            ("truncated frame", lambda request: frame(request).rstrip(b"\n")),
            ("oversized frame", lambda request: frame(request, padding="x" * 70000)),
        ]
        responses = iter(invalid)

        async def peer(reader, writer, connection):
            if connection % 2 == 1:
                request = await self.receive(reader, connection)
                _, make_response = next(responses)
                writer.write(make_response(request))
                await writer.drain()
                return  # EOF must not make a partial JSON frame acceptable.
            await self.echo(reader, writer, connection)

        client = await self.start_client(peer, connect=False)
        for label, _ in invalid:
            with self.subTest(frame=label):
                await self.bounded(client.connect())
                writer = client.writer
                result = await self.bounded(client.send_command("bones.list"))
                self.assertFalse(result["ok"], result)
                self.assertTrue(writer.is_closing())
                self.assertIsNone(client.reader)
                self.assertIsNone(client.writer)
                self.assertEqual(await self.bounded(client.send_command("slots.list")),
                                 {"ok": True, "op": "slots.list"})
                await self.bounded(client.aclose())
        self.assertEqual(self.connections, 2 * len(invalid))

    async def test_request_ids_are_unique_across_independent_clients(self):
        first = await self.start_client(self.echo)
        second = MarrowClient(token="")
        self.clients.append(second)
        await self.bounded(first.send_command("bones.list"))
        await self.bounded(second.send_command("slots.list"))
        self.assertEqual(self.connections, 2)
        self.assertEqual(len({request["id"] for _, request in self.requests}), 2)
        self.assertTrue(all(isinstance(request["id"], str) for _, request in self.requests))

    async def test_arguments_are_preserved_over_the_wire(self):
        client = await self.start_client(self.echo)
        arguments = {"name": "테스트", "keys": [0, 1.5], "enabled": False, "value": None}
        await self.bounded(client.send_command("animation.create", arguments))
        self.assertEqual(self.requests[0][1]["args"], arguments)
        self.assertEqual(self.requests[0][1]["jsonrpc"], "2.0")

    def test_request_timeout_must_be_finite_and_positive(self):
        for value in (0, -1, float("inf"), float("-inf"), float("nan")):
            with self.subTest(timeout=value), self.assertRaises(ValueError):
                MarrowClient(request_timeout=value)


if __name__ == "__main__":
    unittest.main()
