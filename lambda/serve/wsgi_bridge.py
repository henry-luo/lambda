"""
Lambda WSGI Bridge — runs inside persistent Python worker subprocess.

Translates Lambda serve IPC protocol <-> WSGI interface.
Supports Flask, Django, Bottle, and any PEP 3333 compliant app.

Communication: newline-delimited JSON (NDJSON) over stdin/stdout.
Binary bodies are base64-encoded.

Usage:
    python3 wsgi_bridge.py --app myapp:app
"""

import importlib
import json
import base64
import sys
import os
import io
import argparse
import signal


class LambdaWSGIBridge:
    """Bridges Lambda serve IPC messages to WSGI application calls."""

    def __init__(self, app):
        self.app = app

    def handle_request(self, msg):
        """Convert IPC message to WSGI environ, invoke app, return response."""
        body = base64.b64decode(msg.get("body", ""))
        body_stream = io.BytesIO(body)

        # build WSGI environ dict
        environ = {
            "REQUEST_METHOD": msg.get("method", "GET"),
            "SCRIPT_NAME": "",
            "PATH_INFO": msg.get("path", "/"),
            "QUERY_STRING": msg.get("query_string", ""),
            "SERVER_NAME": "localhost",
            "SERVER_PORT": "3000",
            "SERVER_PROTOCOL": "HTTP/1.1",
            "wsgi.version": (1, 0),
            "wsgi.url_scheme": "http",
            "wsgi.input": body_stream,
            "wsgi.errors": sys.stderr,
            "wsgi.multithread": False,
            "wsgi.multiprocess": True,
            "wsgi.run_once": False,
            "CONTENT_LENGTH": str(len(body)),
        }

        # map headers to CGI environ keys
        for name, value in msg.get("headers", []):
            key = "HTTP_" + name.upper().replace("-", "_")
            if name.lower() == "content-type":
                environ["CONTENT_TYPE"] = value
            elif name.lower() == "content-length":
                environ["CONTENT_LENGTH"] = value
            else:
                environ[key] = value

        # call WSGI app
        response_started = False
        status_code = 200
        response_headers = []
        response_body = bytearray()

        def start_response(status, headers, exc_info=None):
            nonlocal response_started, status_code, response_headers
            if exc_info:
                try:
                    if response_started:
                        raise exc_info[1].with_traceback(exc_info[2])
                finally:
                    exc_info = None
            response_started = True
            # parse status code from "200 OK" format
            status_code = int(status.split(" ", 1)[0])
            response_headers = [[name, value] for name, value in headers]

        try:
            result = self.app(environ, start_response)
            try:
                for chunk in result:
                    response_body.extend(chunk)
            finally:
                if hasattr(result, "close"):
                    result.close()
        except Exception as exc:
            sys.stderr.write(f"WSGI app error: {exc}\n")
            sys.stderr.flush()
            return {
                "id": msg.get("id", 0),
                "status": 500,
                "headers": [["content-type", "text/plain"]],
                "body": base64.b64encode(
                    f"Internal Server Error: {exc}".encode()
                ).decode(),
            }

        return {
            "id": msg.get("id", 0),
            "status": status_code,
            "headers": response_headers,
            "body": base64.b64encode(bytes(response_body)).decode(),
        }

    def run(self):
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

            result = self.handle_request(msg)
            sys.stdout.write(json.dumps(result) + "\n")
            sys.stdout.flush()


def load_app(app_string):
    """Load WSGI application from 'module:attribute' string."""
    if ":" not in app_string:
        raise ValueError(f"App string must be 'module:attribute', got '{app_string}'")

    module_path, attr_name = app_string.rsplit(":", 1)

    if os.getcwd() not in sys.path:
        sys.path.insert(0, os.getcwd())

    module = importlib.import_module(module_path)
    app = getattr(module, attr_name)
    return app


def main():
    parser = argparse.ArgumentParser(description="Lambda WSGI Bridge")
    parser.add_argument("--app", required=True, help="WSGI app as 'module:attribute'")
    args = parser.parse_args()

    app = load_app(args.app)
    bridge = LambdaWSGIBridge(app)

    def handle_sigterm(signum, frame):
        sys.exit(0)
    signal.signal(signal.SIGTERM, handle_sigterm)

    bridge.run()


if __name__ == "__main__":
    main()
