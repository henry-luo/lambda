#!/usr/bin/env python3
"""Exercise runtime TeX handoff through the shared loopback fixture server."""

from functools import partial
from http.server import ThreadingHTTPServer
from pathlib import Path
import os
import json
import subprocess
import sys
import threading
import unittest
from urllib.parse import quote

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
        for fixture in ("script_http_basic.tex", "script_auto_unsupported.tex", "script_http_escaped #?.tex"):
            with self.subTest(fixture=fixture):
                port = self.server.server_address[1]
                url = "http://127.0.0.1:%d/%s" % (port, quote(fixture, safe=""))
                self.run_script(fixture,
                    'let url="%s";\n' % url +
                    'let path=\\.http["127.0.0.1:%d"]["%s"];\n' % (port, fixture) +
                    'let source=input(url,"text")^;\n'
                    'let automatic=input(url) ^ { null };\n'
                    'let latex=input(url,"latex") ^ { null };\n'
                    'let latex_direct=parse(source,"latex") ^ { null };\n'
                    'let tex=input(url,"tex") ^ { null };\n'
                    'let tex_direct=parse(source,"tex") ^ { null };\n'
                    'let path_auto=input(path) ^ { null };\n'
                    'let path_latex=input(path,"latex") ^ { null };\n'
                    'let path_tex=input(path,"tex") ^ { null };\n'
                    '{matches:automatic==latex_direct and latex==latex_direct and tex==tex_direct '
                    'and path_auto==latex_direct and path_latex==latex_direct and path_tex==tex_direct}\n')

    def test_remote_document_does_not_fetch_local_input(self):
        QuietHandler.requests.clear()
        url = "http://127.0.0.1:%d/script_http_include.tex" % self.server.server_address[1]
        self.run_script("remote-include",
            'let result=input("%s", "tex")^;\n' % url +
            '{matches:len(result.diagnostics)>0 and not contains(result.text,"Hello")^}\n')
        self.assertIn("/script_http_include.tex", QuietHandler.requests)
        self.assertNotIn("/script_public_input.tex", QuietHandler.requests)

    def test_nul_path_keys_do_not_fetch_a_truncated_resource(self):
        QuietHandler.requests.clear()
        port = self.server.server_address[1]
        self.run_script("nul-path",
            'let host="127.0.0.1:%d";\n' % port +
            'let bad_host=\\.http[host ++ "\\u0000.attacker"]["script_http_basic.tex"];\n'
            'let bad_key=\\.http[host]["script_http_basic.tex\\u0000other"];\n'
            '{matches:(input(bad_host,"latex") ^ { null }) == null and '
            '(input(bad_key,"latex") ^ { null }) == null}\n')
        self.assertEqual(QuietHandler.requests, [])

    def test_native_cli_uses_script_for_local_and_remote_documents(self):
        fixture = ROOT / "test/lambda/latex/fixtures/script_public_latex_include.tex"
        remote = "http://127.0.0.1:%d/script_http_basic.tex" % self.server.server_address[1]
        env = dict(os.environ, LAMBDA_HOME=str(ROOT / "lmd"),
            LAMBDA_TEMP_DIR=str(self.work), LAMBDA_CACHE_DIR=str(self.work / "cache"))
        for name, source in (("local", str(fixture)), ("remote", remote)):
            for declared in (False, True):
                with self.subTest(source=name, explicit=declared):
                    case = name + ("-explicit" if declared else "-auto")
                    output = self.work / ("cli-" + case + ".json")
                    command = [str(ROOT / "lambda.exe"), "convert", source]
                    if declared:
                        command += ["-f", "latex"]
                    command += ["-t", "json", "-o", str(output)]
                    result = subprocess.run(command, cwd=ROOT,
                        env=env, capture_output=True, timeout=120)
                    self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace")[-2000:])
                    self.run_script("cli-compare-" + case,
                        'let document=input(%s,"latex")^;\n' % json.dumps(source) +
                        'let expected=parse(format(document,"json")^,"json")^;\n'
                        'let actual=input(%s,"json")^;\n' % json.dumps(str(output)) +
                        '{matches:actual==expected}\n')

    def test_native_cli_reports_script_parser_failure(self):
        fixture = ROOT / "test/lambda/latex/fixtures/script_auto_unsupported.tex"
        output = self.work / "unsupported.json"
        env = dict(os.environ, LAMBDA_HOME=str(ROOT / "lmd"),
            LAMBDA_TEMP_DIR=str(self.work), LAMBDA_CACHE_DIR=str(self.work / "cache"))
        result = subprocess.run([str(ROOT / "lambda.exe"), "convert", str(fixture),
            "-f", "latex", "-t", "json", "-o", str(output)], cwd=ROOT,
            env=env, capture_output=True, timeout=120)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
