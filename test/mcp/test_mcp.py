#!/usr/bin/env python3
"""Real-transport MCP checks; all scratch data stays below the checkout's temp/."""
import concurrent.futures
import http.client
import http.server
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "lambda.exe"
VERSION = "2026-07-28"


def request(method, params=None, identity="test-1"):
    params = dict(params or {})
    params["_meta"] = {
        "io.modelcontextprotocol/protocolVersion": VERSION,
        "io.modelcontextprotocol/clientCapabilities": {},
    }
    return {"jsonrpc": "2.0", "id": identity, "method": method, "params": params}


def convert(text='{"amount":42}', target="yaml", mime="application/json"):
    return request("tools/call", {"name": "lambda.convert", "arguments": {
        "document": {"kind": "text", "mimeType": mime, "text": text}, "to": target}})


def stdio(*messages):
    data = b"".join((m if isinstance(m, bytes) else json.dumps(m).encode()) + b"\n" for m in messages)
    process = subprocess.run([str(EXE), "mcp"], input=data, capture_output=True,
                             cwd=ROOT, timeout=30)
    if process.returncode:
        raise AssertionError(process.stderr.decode(errors="replace"))
    # parsing each physical line also verifies no banners or pretty-print newlines leak.
    return [json.loads(line) for line in process.stdout.splitlines()]


class StdioTests(unittest.TestCase):
    def test_discovery(self):
        result = stdio(request("server/discover"))[0]["result"]
        self.assertEqual(result["resultType"], "complete")
        self.assertIn(VERSION, result["supportedVersions"])
        self.assertEqual(result["_meta"]["io.modelcontextprotocol/serverInfo"]["name"], "lambda")

    def test_tool_registry(self):
        tools = stdio(request("tools/list"))[0]["result"]["tools"]
        self.assertEqual({t["name"] for t in tools}, {"lambda.convert", "lambda.capabilities"})
        self.assertTrue(all(t["inputSchema"]["type"] == "object" for t in tools))

    def test_convert_and_reuse_process(self):
        replies = stdio(convert(), request("ping", identity="next"))
        document = replies[0]["result"]["structuredContent"]["documents"][0]
        self.assertEqual(document["mimeType"], "application/yaml")
        self.assertIn("amount: 42", document["text"])
        self.assertEqual(document["byteLength"], len(document["text"].encode()))
        self.assertEqual(replies[1]["id"], "next")

    def test_whitespace_and_escapes_survive_framing(self):
        original = {"text": ' line one\n\tline "two" \\ ', "nested": [None, False, "世界"]}
        result = stdio(convert(json.dumps(original), "json"))[0]["result"]
        self.assertEqual(json.loads(result["structuredContent"]["documents"][0]["text"]), original)

    def test_null_document_is_valid(self):
        result = stdio(convert("null", "json"))[0]["result"]
        self.assertFalse(result["isError"])
        self.assertIsNone(json.loads(result["structuredContent"]["documents"][0]["text"]))

    def test_parser_failure_is_tool_error(self):
        response = stdio(convert('{"broken":'))[0]
        self.assertTrue(response["result"]["isError"])
        self.assertNotIn("error", response)

    def test_tool_arguments_follow_published_schema(self):
        message = convert()
        message["params"]["arguments"]["unexpected"] = True
        self.assertTrue(stdio(message)[0]["result"]["isError"])

    def test_unknown_tool_and_method(self):
        replies = stdio(request("tools/call", {"name": "missing"}), request("vendor/unknown"))
        self.assertEqual(replies[0]["error"]["code"], -32602)
        self.assertEqual(replies[1]["error"]["code"], -32601)

    def test_invalid_json_and_envelopes(self):
        replies = stdio(b'{"x":', b'[]', request("ping", identity=None), b'{"jsonrpc":"2.0","id":2,"method":1}')
        self.assertEqual([r["error"]["code"] for r in replies], [-32700, -32600, -32600, -32600])

    def test_invalid_utf8_and_embedded_nul(self):
        replies = stdio(b'\xff', b'{}\x00ignored')
        self.assertEqual([r["error"]["code"] for r in replies], [-32700, -32700])

    def test_no_reply_to_notifications(self):
        notification = request("notifications/cancelled")
        del notification["id"]
        self.assertEqual(stdio(notification), [])

    def test_legacy_lifecycle(self):
        messages = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        ]
        replies = stdio(*messages)
        self.assertEqual(len(replies), 2)
        self.assertEqual(replies[0]["result"]["protocolVersion"], "2025-11-25")
        self.assertNotIn("resultType", replies[1]["result"])

    def test_legacy_notification_cannot_skip_handshake(self):
        replies = stdio({"jsonrpc": "2.0", "method": "notifications/initialized"},
                        {"jsonrpc": "2.0", "id": 1, "method": "tools/list"})
        self.assertEqual(replies[0]["error"]["code"], -32600)

    def test_version_error_is_explicit(self):
        message = request("ping")
        message["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2099-01-01"
        error = stdio(message)[0]["error"]
        self.assertEqual(error["code"], -32022)
        self.assertIn(VERSION, error["data"]["supported"])

    def test_large_frame_is_discarded_completely(self):
        replies = stdio(b"x" * (8 * 1024 * 1024 + 1), request("ping"))
        self.assertEqual(len(replies), 2)
        self.assertIn("error", replies[0])
        self.assertEqual(replies[1]["result"]["resultType"], "complete")


class HttpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            cls.port = sock.getsockname()[1]
        cls.process = subprocess.Popen([str(EXE), "serve", "--mcp", "--port", str(cls.port)],
                                       cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=.2):
                    return
            except OSError:
                if cls.process.poll() is not None:
                    raise AssertionError(cls.process.communicate()[1].decode(errors="replace"))
                time.sleep(.05)
        cls.process.kill()
        raise AssertionError("MCP HTTP listener did not start")

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        try:
            stdout, stderr = cls.process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            cls.process.kill()
            cls.process.communicate()
            raise AssertionError("MCP HTTP shutdown did not drain")
        if cls.process.returncode:
            raise AssertionError(stderr.decode(errors="replace"))
        if stdout:
            raise AssertionError("HTTP server unexpectedly wrote to stdout")

    @classmethod
    def call(cls, message, extra=None):
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
                   "MCP-Protocol-Version": VERSION, "Mcp-Method": message["method"]}
        if message["method"] == "tools/call":
            headers["Mcp-Name"] = message["params"]["name"]
        headers.update(extra or {})
        conn = http.client.HTTPConnection("127.0.0.1", cls.port, timeout=15)
        try:
            conn.request("POST", "/mcp", json.dumps(message), headers)
            response = conn.getresponse()
            return response.status, response.read()
        finally:
            conn.close()

    def test_conversion(self):
        status, body = self.call(convert())
        self.assertEqual(status, 200)
        self.assertFalse(json.loads(body)["result"]["isError"])

    def test_parallel_requests(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            responses = list(pool.map(lambda i: self.call(request("ping", identity=str(i))), range(24)))
        self.assertTrue(all(status == 200 for status, _ in responses))
        self.assertEqual(len({json.loads(body)["id"] for _, body in responses}), 24)

    def test_header_mismatch(self):
        status, body = self.call(request("ping"), {"Mcp-Method": "tools/list"})
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(body)["error"]["code"], -32020)

    def test_http_discovery_only_advertises_its_profile(self):
        status, body = self.call(request("server/discover"))
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["result"]["supportedVersions"], [VERSION])

    def test_unsupported_http_version_returns_protocol_error(self):
        message = request("ping")
        message["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2099-01-01"
        status, body = self.call(message, {"MCP-Protocol-Version": "2099-01-01"})
        self.assertEqual(status, 400)
        self.assertEqual(json.loads(body)["error"]["code"], -32022)

    def test_connection_close_drains_large_reply(self):
        status, body = self.call(convert(json.dumps({"text": "x" * 1000000}), "json"), {"Connection": "close"})
        self.assertEqual(status, 200)
        text = json.loads(body)["result"]["structuredContent"]["documents"][0]["text"]
        self.assertEqual(len(json.loads(text)["text"]), 1000000)

    def test_http_pipelining_preserves_next_request(self):
        frames = []
        for i in range(2):
            data = json.dumps(request("ping", identity=str(i))).encode()
            frames.append((f"POST /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n"
                           f"MCP-Protocol-Version: {VERSION}\r\nMcp-Method: ping\r\n"
                           f"Content-Length: {len(data)}\r\n\r\n").encode() + data)
        with socket.create_connection(("127.0.0.1", self.port), timeout=15) as sock:
            sock.sendall(b"".join(frames))
            stream = sock.makefile("rb")
            for i in range(2):
                self.assertTrue(stream.readline().startswith(b"HTTP/1.1 200"))
                length = None
                while True:
                    line = stream.readline()
                    if line == b"\r\n":
                        break
                    self.assertTrue(line, "response headers ended unexpectedly")
                    if line.lower().startswith(b"content-length:"):
                        length = int(line.split(b":", 1)[1])
                self.assertEqual(json.loads(stream.read(length))["id"], str(i))
            stream.close()

    def test_origin_rejected(self):
        self.assertEqual(self.call(request("ping"), {"Origin": "https://untrusted.example"})[0], 403)

    def test_disconnect_during_worker_and_next_request(self):
        data = json.dumps(convert(json.dumps({"text": "x" * 1000000}))).encode()
        with socket.create_connection(("127.0.0.1", self.port), timeout=15) as sock:
            headers = (f"POST /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n"
                       f"MCP-Protocol-Version: {VERSION}\r\nMcp-Method: tools/call\r\nMcp-Name: lambda.convert\r\n"
                       f"Content-Length: {len(data)}\r\n\r\n").encode()
            sock.sendall(headers + data)
        self.assertEqual(self.call(request("ping"))[0], 200)


class Peer(http.server.BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *_args):
        pass

    def do_POST(self):
        message = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        self.requests.append((message, dict(self.headers)))
        if message["method"] == "vendor/error":
            reply = {"jsonrpc": "2.0", "id": message["id"], "error": {
                "code": -32602, "message": "peer failure", "data": {"detail": "kept"}}}
        else:
            reply = {"jsonrpc": "2.0", "id": message["id"], "result": {
                "resultType": "complete", "isError": message["method"] == "vendor/tool_error",
                "structuredContent": {"answer": 42}, "content": []}}
        if message["method"] == "vendor/wrong_id":
            reply["id"] = "unrelated"
        data = json.dumps(reply).encode()
        if message["method"] == "vendor/sse":
            data = b': keepalive\r\n\r\ndata: ' + data + b'\r\n\r\n'
            content_type = "text/event-stream"
        else:
            content_type = "application/json"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Peer)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def run_script(self, method="vendor/echo", expression="reply.structuredContent.answer", options="", namespace="mcp", tool=False, endpoint_string=False, dry_run=False):
        url = f"http://127.0.0.1:{self.server.server_port}/mcp"
        endpoint = json.dumps(url) if endpoint_string else f'{{url: {json.dumps(url)}{options}}}'
        code = (f'import client: lambda.mcp\npn main() {{\n'
                f' let reply = {namespace}.{"call_tool" if tool else "request"}({endpoint}, '
                f'{json.dumps(method)}, {{text: "hello"}})^\n return {expression}\n}}\n')
        if namespace == "mcp":
            code = code.replace("import client: lambda.mcp", "import mcp")
        with tempfile.TemporaryDirectory(prefix="mcp-client-", dir=ROOT / "temp") as directory:
            path = Path(directory) / "client.ls"
            path.write_text(code)
            path.with_suffix(".txt").write_text("42\n")
            env = dict(os.environ, MCP_TEST_TOKEN="test-secret")
            command = [str(EXE), "run", str(path)]
            if dry_run:
                command.append("--dry-run")
            return subprocess.run(command, cwd=ROOT,
                                  capture_output=True, text=True, timeout=30, env=env)

    def test_generic_method(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "42")
        message, headers = Peer.requests[-1]
        self.assertEqual(message["method"], "vendor/echo")
        self.assertEqual(headers["Mcp-Method"], "vendor/echo")
        self.assertEqual(headers["MCP-Protocol-Version"], VERSION)

    def test_import_alias(self):
        result = self.run_script(namespace="client")
        self.assertEqual(result.stdout.strip(), "42", result.stderr)

    def test_url_string_descriptor(self):
        result = self.run_script(endpoint_string=True)
        self.assertEqual(result.stdout.strip(), "42", result.stderr)

    def test_call_tool(self):
        result = self.run_script(method="example.echo", tool=True)
        self.assertEqual(result.stdout.strip(), "42", result.stderr)
        message, headers = Peer.requests[-1]
        self.assertEqual(message["method"], "tools/call")
        self.assertEqual(message["params"]["name"], "example.echo")
        self.assertEqual(message["params"]["arguments"], {"text": "hello"})
        self.assertEqual(headers["Mcp-Name"], "example.echo")

    def test_sse(self):
        result = self.run_script(method="vendor/sse")
        self.assertEqual(result.stdout.strip(), "42", result.stderr)

    def test_tool_error_is_returned(self):
        result = self.run_script(method="vendor/tool_error", expression="reply.isError")
        self.assertEqual(result.stdout.strip(), "true", result.stderr)

    def test_peer_error_retains_details(self):
        result = self.run_script(method="vendor/error")
        self.assertIn("kept", result.stderr + result.stdout)

    def test_wrong_response_id_rejected(self):
        result = self.run_script(method="vendor/wrong_id")
        self.assertIn("Invalid MCP reply", result.stderr + result.stdout)

    def test_bearer_from_environment(self):
        result = self.run_script(options=', token_env: "MCP_TEST_TOKEN"')
        self.assertEqual(result.stdout.strip(), "42", result.stderr)
        self.assertEqual(Peer.requests[-1][1]["Authorization"], "Bearer test-secret")
        self.assertNotIn("test-secret", result.stdout + result.stderr)

    def test_dry_run_does_not_connect(self):
        before = len(Peer.requests)
        result = self.run_script(dry_run=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("disabled in dry-run mode", result.stdout + result.stderr)
        self.assertEqual(len(Peer.requests), before)


if __name__ == "__main__":
    (ROOT / "temp").mkdir(exist_ok=True)
    unittest.main(verbosity=2)
