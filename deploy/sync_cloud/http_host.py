"""HTTP/process plumbing only. Lambda Script owns authentication and storage policy."""
from pathlib import Path
import argparse
import http.server
import os
import subprocess
import tempfile
import threading

MAX_REQUEST = 131072
MAX_RESPONSE = 1048576


def invoke(body, *, root=None, storage=None):
    """One request, no shared application state; usable by a provider HTTP adapter."""
    if not isinstance(body, bytes) or not 0 < len(body) <= MAX_REQUEST:
        return 413, b"request size rejected"
    root = Path(root or os.environ.get("LAMBDA_ROOT") or Path(__file__).resolve().parents[2]).resolve()
    storage = storage if storage is not None else os.environ["LAMBDA_CLOUD_STORAGE"]
    temporary = root / "temp" / "sync-cloud-function"
    temporary.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="request-", dir=temporary) as directory:
        event = Path(directory) / "event.json"
        result = Path(directory) / "response.json"
        event.write_bytes(body)
        env = dict(os.environ, LAMBDA_ROOT=str(root), LAMBDA_CLOUD_STORAGE=storage,
                   LAMBDA_CLOUD_EVENT=str(event), LAMBDA_CLOUD_RESULT=str(result))
        try:
            child = subprocess.run([str(root / "lambda.exe"), "--no-log", "run",
                                    "lmd/package/sync/cloud_function_cli.ls"], cwd=root,
                                   env=env, capture_output=True, timeout=60)
        except subprocess.TimeoutExpired:
            return 504, b"function deadline exceeded"
        if child.returncode or not result.is_file() or result.stat().st_size > MAX_RESPONSE:
            # Interpreter diagnostics can contain URLs; they are not an HTTP response.
            return 500, b"function invocation failed"
        return 200, result.read_bytes()


def handler(event, context=None):
    """Convenience function-URL envelope adapter; other hosts can call invoke directly."""
    import base64
    body = event.get("body", "")
    try:
        raw = base64.b64decode(body, validate=True) if event.get("isBase64Encoded") else body.encode("utf-8")
    except (ValueError, TypeError, AttributeError):
        return {"statusCode": 400, "body": "invalid request"}
    status, response = invoke(raw)
    return {"statusCode": status, "headers": {"content-type": "application/json", "cache-control": "no-store"},
            "body": response.decode("utf-8")}


def main():
    parser = argparse.ArgumentParser(description="Local/cloud-container HTTP adapter for the stateless script function")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8080")))
    parser.add_argument("--ready", type=Path)
    args = parser.parse_args()
    slots = threading.BoundedSemaphore(4)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            if self.path != "/sync" or self.headers.get("Transfer-Encoding"):
                self.send_error(400)
                return
            try:
                size = int(self.headers.get("Content-Length", "-1"))
            except ValueError:
                size = -1
            if not 0 < size <= MAX_REQUEST:
                self.send_error(413)
                return
            self.connection.settimeout(65)
            raw = self.rfile.read(size)
            if len(raw) != size:
                self.send_error(400)
                return
            if not slots.acquire(blocking=False):
                self.send_error(503)
                return
            try:
                status, response = invoke(raw)
            finally:
                slots.release()
            self.send_response(status)
            self.send_header("content-type", "application/json")
            self.send_header("cache-control", "no-store")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)

    server = http.server.ThreadingHTTPServer((args.bind, args.port), Handler)
    if args.ready:
        args.ready.write_text(str(server.server_port))
    try:
        server.serve_forever(poll_interval=0.2)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
