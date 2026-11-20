#!/usr/bin/env python3
"""Exercise Lambda Script handlers through the native HTTP listener."""

import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
EXECUTABLE = ROOT / "lambda.exe"


class ScriptServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "temp").mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="script-server-", dir=ROOT / "temp")
        cls.public = Path(cls.temp.name) / "public"
        cls.public.mkdir()
        (cls.public / "index.html").write_text("<html>Acro shell</html>")
        (cls.public / "test.txt").write_text("static asset")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            cls.port = probe.getsockname()[1]
        env = {**os.environ, "LAMBDA_HOME": str(ROOT / "lmd")}
        cls.server = subprocess.Popen(
            [str(EXECUTABLE), "serve", "--host", "127.0.0.1", "--port", str(cls.port),
             "--dir", str(cls.public), "--spa", "/app", "test/serve/handler.ls"],
            cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            with socket.socket() as probe:
                probe.settimeout(0.2)
                if probe.connect_ex(("127.0.0.1", cls.port)) == 0:
                    return
            if cls.server.poll() is not None:
                raise RuntimeError(cls.server.communicate()[1].decode(errors="replace"))
            time.sleep(0.05)
        cls.server.kill()
        raise RuntimeError("Script HTTP server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.server.terminate()
        try:
            _, error = cls.server.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            cls.server.kill()
            _, error = cls.server.communicate()
        cls.temp.cleanup()
        if cls.server.returncode:
            raise RuntimeError(error.decode(errors="replace"))

    def request(self, method, path, body=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=20)
        try:
            conn.request(method, path, body=body, headers={"Content-Type": "application/json"})
            response = conn.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            conn.close()

    def test_script_request(self):
        status, headers, body = self.request("POST", "/echo?one=two", '{"text":"hello"}')
        self.assertEqual(status, 201)
        self.assertTrue(headers["Content-Type"].startswith("application/json"))
        self.assertEqual(json.loads(body), {"method": "POST", "path": "/echo", "body": '{"text":"hello"}'})

    def test_binary(self):
        status, headers, body = self.request("GET", "/binary")
        self.assertEqual((status, headers["Content-Type"], body), (200, "application/octet-stream", b"\x00\xffA"))

    def test_static_and_spa(self):
        self.assertEqual(self.request("GET", "/test.txt")[2], b"static asset")
        self.assertIn(b"Acro shell", self.request("GET", "/app/countries/new")[2])

    def test_invalid_startup(self):
        invalid = Path(self.temp.name) / "invalid.ls"
        invalid.write_text('pub fn other(request) => request\n')
        env = {**os.environ, "LAMBDA_HOME": str(ROOT / "lmd")}
        command = [str(EXECUTABLE), "serve", "--port", str(self.port), str(invalid)]
        failed = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=10)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn(b"handle(request)", failed.stderr)
        env["UV_THREADPOOL_SIZE"] = "1"
        command[-1] = "test/serve/handler.ls"
        insufficient = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=10)
        self.assertNotEqual(insufficient.returncode, 0)
        self.assertIn(b"at least two", insufficient.stderr)

    def test_handler_error(self):
        self.assertEqual(self.request("GET", "/failure")[0], 500)
        self.assertEqual(self.request("GET", "/bad-header")[0], 500)

    def test_concurrent_requests(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda i: self.request("POST", "/echo", str(i)), range(12)))
        self.assertEqual([status for status, _, _ in results], [201] * 12)
        self.assertEqual([json.loads(body)["body"] for _, _, body in results], [str(i) for i in range(12)])


if __name__ == "__main__":
    unittest.main()
