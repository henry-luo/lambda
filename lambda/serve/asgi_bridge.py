"""
Lambda ASGI Bridge — runs inside persistent Python worker subprocess.

Translates Lambda serve IPC protocol <-> ASGI interface.
Supports FastAPI, Starlette, Litestar, and any ASGI 3.0 compliant app.

Communication: newline-delimited JSON (NDJSON) over stdin/stdout or Unix domain sockets.
Binary bodies are base64-encoded.

Usage:
    python3 asgi_bridge.py --app myapp:app                          # pipe transport
    python3 asgi_bridge.py --app myapp:app --uds /path/to.sock      # UDS transport
"""

import asyncio
import importlib
import json
import base64
import sys
import os
import argparse
import socket
import signal


class LambdaASGIBridge:
    """Bridges Lambda serve IPC messages to ASGI application calls."""

    def __init__(self, app):
        self.app = app
        self.loop = asyncio.new_event_loop()

    async def handle_request(self, msg):
        """Convert IPC message to ASGI scope/receive/send, invoke app."""
        scope = {
            "type": "http",
            "asgi": {"version": "3.0", "spec_version": "2.4"},
            "http_version": "1.1",
            "method": msg.get("method", "GET"),
            "path": msg.get("path", "/"),
            "query_string": msg.get("query_string", "").encode("latin-1"),
            "root_path": "",
            "headers": [
                (k.encode("latin-1"), v.encode("latin-1"))
                for k, v in msg.get("headers", [])
            ],
            "server": ("localhost", 3000),
        }

        body = base64.b64decode(msg.get("body", ""))
        request_complete = False

        async def receive():
            nonlocal request_complete
            if not request_complete:
                request_complete = True
                return {"type": "http.request", "body": body, "more_body": False}
            await asyncio.Future()  # block forever (wait for disconnect)

        response_started = False
        response_status = 200
        response_headers = []
        response_body = bytearray()

        async def send(event):
            nonlocal response_started, response_status, response_headers, response_body
            if event["type"] == "http.response.start":
                response_started = True
                response_status = event["status"]
                response_headers = [
                    (
                        k.decode("latin-1") if isinstance(k, bytes) else k,
                        v.decode("latin-1") if isinstance(v, bytes) else v,
                    )
                    for k, v in event.get("headers", [])
                ]
            elif event["type"] == "http.response.body":
                chunk = event.get("body", b"")
                if isinstance(chunk, memoryview):
                    chunk = bytes(chunk)
                response_body.extend(chunk)

        try:
            await self.app(scope, receive, send)
        except Exception as exc:
            sys.stderr.write(f"ASGI app error: {exc}\n")
            sys.stderr.flush()
            return {
                "id": msg.get("id", 0),
                "status": 500,
                "headers": [["content-type", "text/plain"]],
                "body": base64.b64encode(f"Internal Server Error: {exc}".encode()).decode(),
            }

        return {
            "id": msg.get("id", 0),
            "status": response_status,
            "headers": response_headers,
            "body": base64.b64encode(bytes(response_body)).decode(),
        }

    def run_pipe(self):
        """Main loop: read JSON requests from stdin, write JSON responses to stdout."""
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError as e:
                sys.stderr.write(f"JSON decode error: {e}\n")
                sys.stderr.flush()
                continue

            result = self.loop.run_until_complete(self.handle_request(msg))
            sys.stdout.write(json.dumps(result) + "\n")
            sys.stdout.flush()

    def run_uds(self, socket_path):
        """Main loop over Unix domain socket."""
        if os.path.exists(socket_path):
            os.unlink(socket_path)

        server_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server_sock.bind(socket_path)
        server_sock.listen(1)
        sys.stderr.write(f"ASGI bridge listening on {socket_path}\n")
        sys.stderr.flush()

        try:
            conn, _ = server_sock.accept()
            reader = conn.makefile("r")
            writer = conn.makefile("wb")

            for line in reader:
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError as e:
                    sys.stderr.write(f"JSON decode error: {e}\n")
                    sys.stderr.flush()
                    continue

                result = self.loop.run_until_complete(self.handle_request(msg))
                writer.write(json.dumps(result).encode() + b"\n")
                writer.flush()

            reader.close()
            writer.close()
            conn.close()
        finally:
            server_sock.close()
            if os.path.exists(socket_path):
                os.unlink(socket_path)


def load_app(app_string):
    """Load ASGI application from 'module:attribute' string."""
    if ":" not in app_string:
        raise ValueError(f"App string must be 'module:attribute', got '{app_string}'")

    module_path, attr_name = app_string.rsplit(":", 1)

    # add current directory to path for local imports
    if os.getcwd() not in sys.path:
        sys.path.insert(0, os.getcwd())

    module = importlib.import_module(module_path)
    app = getattr(module, attr_name)
    return app


def main():
    parser = argparse.ArgumentParser(description="Lambda ASGI Bridge")
    parser.add_argument("--app", required=True, help="ASGI app as 'module:attribute'")
    parser.add_argument("--uds", default=None, help="Unix domain socket path")
    args = parser.parse_args()

    app = load_app(args.app)
    bridge = LambdaASGIBridge(app)

    # handle SIGTERM gracefully
    def handle_sigterm(signum, frame):
        sys.exit(0)
    signal.signal(signal.SIGTERM, handle_sigterm)

    if args.uds:
        bridge.run_uds(args.uds)
    else:
        bridge.run_pipe()


if __name__ == "__main__":
    main()
