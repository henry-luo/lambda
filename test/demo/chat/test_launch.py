#!/usr/bin/env python3
"""Exercise the public multi-window CLI, including validation and cleanup."""
import contextlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import time
import unittest
import uuid

from test_shared_clients import ROOT, attribute, count, fixture, verify_history


class ChatLaunchTests(unittest.TestCase):
    def setUp(self):
        self.artifacts = ROOT / "temp/chat-launch" / uuid.uuid4().hex
        self.artifacts.mkdir(parents=True)
        self.executable = str(Path(os.environ.get("LAMBDA_CHAT_EXE", ROOT / "lambda.exe")).resolve())
        self.env = dict(os.environ, CHAT_DEMO_ROOT=str(self.artifacts),
                        LAMBDA_LOG_FILE=str(self.artifacts / "launcher-runtime.log"))
        for key in ("CHAT_DEMO_ENDPOINT", "CHAT_DEMO_USER", "CHAT_DEMO_PEER", "CHAT_DEMO_PORT"):
            self.env.pop(key, None)

    def write_fixture(self, user, events=None):
        data = fixture(user, None) if events is None else {
            "html": "test/demo/chat/chat.ls", "viewport": {"width": 1000, "height": 760}, "events": events}
        (self.artifacts / (user + ".json")).write_text(json.dumps(data))

    @contextlib.contextmanager
    def running(self, users, extra=()):
        log_path = self.artifacts / "launcher.log"
        with log_path.open("w") as log:
            args = [self.executable, "demo", "chat", "user=" + users, *extra]
            process = subprocess.Popen(args, cwd=ROOT, env=self.env, stdout=log, stderr=log)
            try:
                yield process
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    def view_options(self):
        return ["--headless", "--event-file", str(self.artifacts / "{user}.json"),
                "--event-result", str(self.artifacts / "{user}-result.json")]

    def endpoint(self):
        log = (self.artifacts / "launcher.log").read_text()
        matches = re.findall(r"Chat server listening at (ws://127\.0\.0\.1:\d+/chat)", log)
        self.assertEqual(len(matches), 1, log)
        return matches[0]

    def assert_server_closed(self):
        port = int(self.endpoint().split(":")[2].split("/")[0])
        with socket.socket() as probe:
            probe.settimeout(1)
            self.assertNotEqual(probe.connect_ex(("127.0.0.1", port)), 0, "launcher left its server listening")

    def test_two_windows_exchange_messages(self):
        for user in ("maya", "alex"):
            self.write_fixture(user)
        with self.running("maya,alex", self.view_options()) as process:
            self.assertEqual(process.wait(timeout=90), 0, str(self.artifacts))
        for user in ("maya", "alex"):
            result = json.loads((self.artifacts / (user + "-result.json")).read_text())
            self.assertEqual(result["result"], "PASS", result)
        verify_history(self.artifacts)
        self.assert_server_closed()

    def test_each_requested_profile_gets_one_window(self):
        users = ("maya", "alex", "noah", "priya")
        for user in users:
            self.write_fixture(user, [attribute("data-user", user), attribute("data-mode", "client"),
                                      attribute("data-online", "true"), count("#profile[disabled]", 1)])
        with self.running(",".join(users), self.view_options()) as process:
            self.assertEqual(process.wait(timeout=90), 0, str(self.artifacts))
        for user in users:
            result = json.loads((self.artifacts / (user + "-result.json")).read_text())
            self.assertEqual(result["result"], "PASS", result)
        self.assert_server_closed()

    def test_invalid_user_lists_fail_before_server_start(self):
        for users, message in [("", "comma-separated"), ("alex,", "comma-separated"),
                               ("maya,maya", "only once"), ("missing", "unknown user")]:
            with self.subTest(users=users), self.running(users) as process:
                self.assertNotEqual(process.wait(timeout=10), 0)
                output = (self.artifacts / "launcher.log").read_text()
                self.assertIn(message, output)
                self.assertNotIn("Chat server listening", output)
                self.assertFalse((self.artifacts / "history.json").exists())

    def test_failed_window_stops_session(self):
        failure = attribute("data-user", "invalid")
        failure["assert_timeout"] = 100
        pending = count("#never-present", 1)
        pending["assert_timeout"] = 60000
        self.write_fixture("maya", [failure])
        self.write_fixture("alex", [pending])
        with self.running("maya,alex", self.view_options()) as process:
            self.assertNotEqual(process.wait(timeout=15), 0)
        self.assert_server_closed()

    def test_server_failure_does_not_open_windows(self):
        (self.artifacts / "history.json").write_text('{}')
        with self.running("maya,alex", self.view_options()) as process:
            self.assertNotEqual(process.wait(timeout=10), 0)
        output = (self.artifacts / "launcher.log").read_text()
        self.assertIn("unsupported history file", output)
        self.assertNotIn("Opening chat windows", output)

    @unittest.skipIf(os.name == "nt", "POSIX SIGINT lifecycle check")
    def test_interrupt_cancels_clients_and_server(self):
        for user in ("maya", "alex"):
            self.write_fixture(user, [count("#never-present", 1)])
        with self.running("maya,alex", self.view_options()) as process:
            deadline = time.monotonic() + 20
            while not all((self.artifacts / ("client-" + user + ".log")).exists()
                          for user in ("maya", "alex")) and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.02)
            self.assertIsNone(process.poll(), str(self.artifacts))
            self.assertTrue(all((self.artifacts / ("client-" + user + ".log")).exists()
                                for user in ("maya", "alex")), str(self.artifacts))
            process.send_signal(signal.SIGINT)
            self.assertEqual(process.wait(timeout=15), 128 + signal.SIGINT)
        self.assert_server_closed()


if __name__ == "__main__":
    unittest.main(verbosity=2)
