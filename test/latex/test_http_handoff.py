#!/usr/bin/env python3
"""Exercise runtime TeX handoff through the shared loopback fixture server."""

from functools import partial
from http.server import ThreadingHTTPServer
from pathlib import Path
import os
import subprocess
import sys
import threading
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test"))
from network_fixture_server import FixtureHandler


class QuietHandler(FixtureHandler):
    requests = []

    def do_GET(self):
        self.requests.append(self.path)
        super().do_GET()

    def log_message(self, *args):
        pass


class TexHttpHandoffTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixtures = ROOT / "test/lambda/latex/fixtures"
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(fixtures)))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.work = ROOT / "temp" / ("tex-http-test-" + str(os.getpid()))
        cls.work.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def run_script(self, name, source):
        expected = b"{\n  matches: true\n}\n"
        script = self.work / (name + ".ls")
        script.write_text(source)
        script.with_suffix(".txt").write_bytes(expected)
        env = dict(os.environ, LAMBDA_HOME=str(ROOT / "lmd"),
            LAMBDA_TEMP_DIR=str(self.work), LAMBDA_CACHE_DIR=str(self.work / "cache"))
        result = subprocess.run([str(ROOT / "lambda.exe"), "--no-log", str(script)],
            cwd=ROOT, env=env, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-2000:])
        self.assertEqual(result.stdout, expected)

    def test_detected_and_explicit_paths_use_script(self):
        for fixture in ("script_http_basic.tex", "script_auto_unsupported.tex"):
            with self.subTest(fixture=fixture):
                url = "http://127.0.0.1:%d/%s" % (self.server.server_address[1], fixture)
                self.run_script(fixture,
                    'let url="%s";\n' % url +
                    'let source=input(url,"text")^;\n'
                    'let automatic=input(url) ^ { null };\n'
                    'let latex=input(url,"latex") ^ { null };\n'
                    'let latex_direct=parse(source,"latex") ^ { null };\n'
                    'let tex=input(url,"tex") ^ { null };\n'
                    'let tex_direct=parse(source,"tex") ^ { null };\n'
                    '{matches:automatic==latex_direct and latex==latex_direct and tex==tex_direct}\n')

    def test_remote_document_does_not_fetch_local_input(self):
        QuietHandler.requests.clear()
        url = "http://127.0.0.1:%d/script_http_include.tex" % self.server.server_address[1]
        self.run_script("remote-include",
            'let result=input("%s", "tex")^;\n' % url +
            '{matches:len(result.diagnostics)>0 and not contains(result.text,"Hello")^}\n')
        self.assertIn("/script_http_include.tex", QuietHandler.requests)
        self.assertNotIn("/script_public_input.tex", QuietHandler.requests)


if __name__ == "__main__":
    unittest.main()
