"""Dependency-free, single-flight transport for the Marrow agent socket.

A failed exchange is never retried: an edit may have executed even when its
response was lost. A later explicit command reconnects with a fresh request ID.
"""

from __future__ import annotations

import asyncio
import json
import math
import os
import uuid
from typing import Any


class MarrowClient:
    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 9876,
        token: str | None = None,
        *,
        request_timeout: float = 30.0,
    ):
        if not math.isfinite(request_timeout) or request_timeout <= 0:
            raise ValueError("request_timeout must be finite and positive")
        self.host = host
        self.port = int(os.environ.get("MARROW_AGENT_PORT", port))
        self.token = token if token is not None else os.environ.get("MARROW_AGENT_TOKEN", "")
        self.request_timeout = request_timeout
        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self._lock = asyncio.Lock()

    async def connect(self) -> None:
        """Connect and authenticate, serialized with requests and explicit close."""
        async with self._lock:
            try:
                await asyncio.wait_for(self._connect_locked(), self.request_timeout)
            except BaseException:
                await self._close_locked()
                raise

    async def aclose(self) -> None:
        """Close the current connection; safe to repeat and reconnect afterwards."""
        async with self._lock:
            await self._close_locked()

    async def send_command(self, op: str, args: dict[str, Any] | None = None) -> dict[str, Any]:
        """Run one exchange. The timeout starts after acquiring the request lock.

        Cancellation while queued has no effect on the active request. Once this
        call owns the connection, cancellation invalidates it and propagates to
        the caller instead of being converted to a normal tool result.
        """
        async with self._lock:
            try:
                return await asyncio.wait_for(self._exchange_locked(op, args), self.request_timeout)
            except asyncio.CancelledError:
                await self._close_locked()
                raise
            except asyncio.TimeoutError:
                await self._close_locked()
                return {
                    "ok": False,
                    "message": (
                        f"Marrow request timed out after {self.request_timeout:g}s; "
                        "execution outcome may be unknown. Command was not retried."
                    ),
                }
            except Exception as error:
                await self._close_locked()
                return {"ok": False, "message": f"Connection error: {error}"}

    async def _connect_locked(self) -> None:
        if self.writer is not None and not self.writer.is_closing():
            return
        await self._close_locked()
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
        if self.token:
            self.writer.write((self.token + "\n").encode())
            await self.writer.drain()
            try:
                acknowledgement = await self._read_result_locked("auth")
            except (ConnectionError, ValueError) as error:
                raise ConnectionError("Marrow agent token rejected or malformed acknowledgement") from error
            if acknowledgement["ok"] is not True:
                raise ConnectionError("Marrow agent token rejected")

    async def _exchange_locked(self, op: str, args: dict[str, Any] | None) -> dict[str, Any]:
        await self._connect_locked()
        # IDs must remain unique across reconnects AND independent client objects:
        # the C++ dispatcher matches responses in a shared queue by string ID.
        request_id = "mcp-" + uuid.uuid4().hex
        request = {"jsonrpc": "2.0", "id": request_id, "op": op, "args": args or {}}
        self.writer.write((json.dumps(request) + "\n").encode())
        await self.writer.drain()
        return await self._read_result_locked(request_id)

    async def _read_result_locked(self, request_id: str) -> dict[str, Any]:
        line = await self.reader.readline()
        if not line:
            raise ConnectionError("Connection closed by Marrow")
        if not line.endswith(b"\n"):
            raise ValueError("Incomplete Marrow response frame")
        response = json.loads(line)
        if not isinstance(response, dict) or response.get("jsonrpc") != "2.0":
            raise ValueError("Malformed JSON-RPC response")
        if response.get("id") != request_id:
            raise ValueError("Response ID does not match the active request")
        if "error" in response:
            raise ValueError("Marrow returned a JSON-RPC error")
        result = response.get("result")
        if not isinstance(result, dict) or not isinstance(result.get("ok"), bool):
            raise ValueError("Malformed Marrow result")
        return result

    async def _close_locked(self) -> None:
        writer = self.writer
        # Detach before the first await, so even repeated cancellation cannot
        # expose the old stream to the next owner of the lock.
        self.reader = None
        self.writer = None
        if writer is None:
            return
        writer.close()
        try:
            # Bound cleanup too; a peer that stops reading must not retain the
            # request lock indefinitely while buffered writes are being flushed.
            await asyncio.wait_for(writer.wait_closed(), timeout=1.0)
        except asyncio.CancelledError:
            writer.transport.abort()
            raise
        except Exception:
            writer.transport.abort()
