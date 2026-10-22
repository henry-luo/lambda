#!/usr/bin/env python3
"""lambda.io.mail against scripted loopback servers (vibe/Lambda_Demo_Mail.md §8).

No network: fake_mail.py serves IMAP, POP3 and SMTP on 127.0.0.1, and the
scripts reach them with `tls: false`. Scratch data stays under temp/mail/.
"""

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
EXE = ROOT / "lambda.exe"
TEMP = ROOT / "temp" / "mail"
sys.path.insert(0, str(HERE))

import fake_mail  # noqa: E402

BINARY = re.compile(r"b'\\x([0-9A-Fa-f]*)'")


def norm(text):
    """`print` joins its arguments with spaces: compare lines with runs of spaces collapsed."""
    return "\n".join(" ".join(line.split()) for line in text.splitlines())


def binaries(text):
    """Every binary value printed in `text`, as bytes."""
    return [bytes.fromhex(h) for h in BINARY.findall(text)]


class MailTests(unittest.TestCase):
    def setUp(self):
        self.state, self.servers = fake_mail.start_all()
        TEMP.mkdir(parents=True, exist_ok=True)
        ports = {name: server.port for name, server in self.servers.items()}
        (TEMP / "test_ports.json").write_text(json.dumps(ports))
        self.spill = TEMP / "spill"
        shutil.rmtree(self.spill, ignore_errors=True)
        self.spill.mkdir()

    def tearDown(self):
        for server in self.servers.values():
            server.stop()

    def run_script(self, name, expect_ok=True):
        process = subprocess.run([str(EXE), "run", str(HERE / name)], cwd=ROOT,
                                 capture_output=True, timeout=90)
        out = process.stdout.decode(errors="replace")
        err = process.stderr.decode(errors="replace")
        if expect_ok and process.returncode != 0:
            self.fail(f"{name} exited {process.returncode}\nstdout:\n{out}\nstderr:\n{err}")
        return norm(out), err, process.returncode

    def test_imap_session(self):
        out, _, _ = self.run_script("_imap_session.ls")
        self.assertIn("select ok ['READ-WRITE'] SELECT completed", out)
        self.assertIn("\n[4, 'EXISTS']\n", out)
        self.assertIn("\n['OK', ['UIDVALIDITY', 1700000001], \"UIDs valid\"]\n", out)
        self.assertIn("fetch [2, 'FETCH', ['UID', 2, 'FLAGS', ['\\\\Seen', '\\\\Flagged']]]", out)
        self.assertEqual(out.count("fetch ["), 4)
        self.assertIn(f"body binary {len(fake_mail.HOSTILE)}", out)
        # the hostile body went out and came back byte for byte
        self.assertEqual(len(self.state.appended), 1)
        self.assertEqual(self.state.appended[0][2], [fake_mail.HOSTILE])
        self.assertIn(f"append ok ['APPENDUID', {fake_mail.UIDVALIDITY}, 1]", out)
        self.assertIn("spilled string", out)
        spilled = list(self.spill.iterdir())
        self.assertEqual(len(spilled), 1)
        self.assertEqual(spilled[0].read_bytes(), fake_mail.BIG)
        self.assertIn("refused: io.mail.command: NO [TRYCREATE] Mailbox doesn't exist", out)
        self.assertIn("noop ok", out)
        self.assertEqual(self.state.logins[0][:3], ("PLAIN", "ada", "secret"))

    def test_imap_idle_and_cancel(self):
        out, _, _ = self.run_script("_imap_idle.ls")
        self.assertIn("event [[5, 'EXISTS']]", out)
        self.assertIn("quiet []", out)
        self.assertIn("noop ok", out)
        self.assertIn("cancelled: task cancelled", out)
        # two finished IDLEs on the main session, one in the task, and the
        # cancelled one ended by DONE as its session closed
        self.assertGreaterEqual(sum(1 for line in self.state.lines if line.upper() == b"DONE"), 3)

    def test_imap_auth(self):
        out, _, _ = self.run_script("_imap_auth.ls")
        self.assertIn("token ok", out)
        self.assertIn("qualified ok", out)
        self.assertTrue(any(login[3] == fake_mail.TOKEN for login in self.state.logins))
        self.assertIn("denied: io.mail.open: login denied", out)
        self.assertIn("refused: io.mail.open: tls: false is allowed only for a loopback server", out)
        self.assertIn("scheme: io.mail.open: the url must start with imaps://", out)
        self.assertIn("missing: io.mail.open: give exactly one of the options 'password' and 'token'", out)

    def test_pop3_session(self):
        out, _, _ = self.run_script("_pop3_session.ls")
        self.assertIn("stat ok 2 ", out)
        payloads = binaries(out)
        self.assertEqual(payloads[0], b"1 uid-alpha\r\n2 uid-beta\r\n")
        # dot-unstuffed, with the body's own line ends
        self.assertEqual(payloads[1], self.state.pop3[0][1])
        self.assertIn("refused: io.mail.command: -ERR no such message", out)

    def test_smtp_send(self):
        out, _, _ = self.run_script("_smtp_send.ls")
        self.assertIn("rejected [\"nobody@example.org\"]", out)
        self.assertEqual(len(self.state.sent), 1)
        sender, recipients, body = self.state.sent[0]
        self.assertEqual(sender, "ada@example.org")
        self.assertEqual(recipients, ["bob@example.org"])
        self.assertEqual(body, b"From: ada@example.org\r\nTo: bob@example.org\r\nSubject: hello\r\n\r\n"
                               b"Hi Bob,\r\n.a line starting with a dot\r\n")

    def test_import_needs_alias(self):
        out, err, code = self.run_script("_import_global.ls", expect_ok=False)
        self.assertNotEqual(code, 0)
        self.assertIn("import lambda.io.mail needs an alias", out + err)


if __name__ == "__main__":
    unittest.main()
