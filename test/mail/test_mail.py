#!/usr/bin/env python3
"""lambda.io.mail against scripted loopback servers (vibe/Lambda_Demo_Mail.md §8).

No network: fake_mail.py serves IMAP, POP3 and SMTP on 127.0.0.1, and the
scripts reach them with `tls: false`. Scratch data stays under temp/mail/.
"""

import json
import os
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
        self.assertIn("denied: io.mail.open: login denied: NO [AUTHENTICATIONFAILED] Invalid credentials", out)
        self.assertIn("pop3 denied: io.mail.open: login denied: -ERR [AUTH] invalid login", out)
        self.assertIn("smtp denied: io.mail.send: login denied: 535-5.7.8 Username and Password not accepted. "
                      "For more information, go to 535 5.7.8 https://support.example.com/BadCredentials", out)
        # the reason never echoes what the client sent
        self.assertNotIn("wrong", out)
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

    def test_pop3_sync(self):
        store = TEMP / "pop3-store"
        shutil.rmtree(store, ignore_errors=True)
        out, _, _ = self.run_script("_pop3_sync.ls")
        self.assertIn("downloaded 2", out)
        self.assertIn("entry 1 uid-alpha pop-uid-alpha pop 41 true", out)
        self.assertIn("seen [\"\\\\Seen\"]", out)
        # a second round finds nothing new; a new delivery is the only download
        self.state.pop3.append(("uid-gamma", b"Subject: third\r\n\r\nagain\r\n"))
        out, _, _ = self.run_script("_pop3_sync.ls")
        self.assertIn("downloaded 1", out)
        self.assertNotIn("downloaded 2", out)
        self.assertIn("entries 3 uidnext 4", out)
        commands = [line.split(b" ")[0] for line in self.state.lines]
        self.assertEqual(commands.count(b"RETR"), 3)
        self.assertNotIn(b"DELE", commands)
        stored = sorted(p.read_bytes() for p in store.rglob("*.eml"))
        self.assertEqual(stored, sorted(body for _, body in self.state.pop3))

    def test_oauth(self):
        out, _, _ = self.run_script("_oauth.ls")
        self.assertIn("signed in tok-123 ref-1 3600", out)
        self.assertIn("userinfo ada@example.org Ada Lovelace", out)
        self.assertIn("bad token: oauth: the userinfo request failed: status 401", out)
        self.assertIn("refreshed tok-123 null", out)
        self.assertIn("stale: oauth: the token request failed: invalid_grant: Token has been expired or revoked.", out)
        self.assertIn("refused: oauth: sign-in refused: access_denied: The user said no", out)
        self.assertIn("client: oauth: the token request failed: invalid_client", out)
        self.assertIn("imap ok", out)
        authorize = [fields for endpoint, fields in self.state.oauth if endpoint == "authorize"]
        tokens = [fields for endpoint, fields in self.state.oauth if endpoint == "token"]
        # PKCE (RFC 7636): an S256 challenge up front, its verifier at the exchange
        self.assertTrue(all(a["code_challenge_method"] == "S256" and a["access_type"] == "offline" for a in authorize))
        exchange = [t for t in tokens if t["grant_type"] == "authorization_code"][0]
        self.assertEqual(len(exchange["code_verifier"]), 64)
        self.assertEqual(fake_mail.pkce_challenge(exchange["code_verifier"]), authorize[0]["code_challenge"])
        # each sign-in carries a fresh state
        self.assertEqual(len({a["state"] for a in authorize}), len(authorize))

    @unittest.skipUnless(sys.platform == "darwin", "the refresh token goes to the Keychain (macOS only, MQ7)")
    def test_sync_oauth(self):
        forget = ["security", "delete-generic-password", "-s", "Lambda Mail (oauth test)", "-a", "ada-oauth"]
        shutil.rmtree(TEMP / "oauth-store", ignore_errors=True)
        try:
            out, _, _ = self.run_script("_sync_oauth.ls")
        finally:
            subprocess.run(forget, capture_output=True)
        self.assertIn("status connecting", out)
        self.assertIn("index INBOX 4", out)
        self.assertIn("deleted true", out)
        # the task refreshed the token and logged in with it, never with a password
        self.assertTrue(any(f.get("grant_type") == "refresh_token" for endpoint, f in self.state.oauth if endpoint == "token"))
        self.assertTrue(any(login[3] == fake_mail.TOKEN for login in self.state.logins))
        self.assertFalse(any(login[2] for login in self.state.logins))

    @unittest.skipUnless(sys.platform == "darwin", "the refresh token goes to the Keychain (macOS only, MQ7)")
    def test_signin_gmail(self):
        forget = ["security", "delete-generic-password", "-s", "Lambda Mail (signin test)", "-a", "ada-example-org"]
        try:
            out, _, _ = self.run_script("_signin_gmail.ls")
        finally:
            subprocess.run(forget, capture_output=True)
        # the address came from userinfo; the password account of that address turned to OAuth
        self.assertIn("replaced ada-example-org ada@example.org Ada (password) oauth gmail imap.gmail.com true", out)
        self.assertIn("token ref-1", out)
        self.assertIn("new ada-example-org Ada Lovelace false", out)
        self.assertIn("deleted true", out)
        self.assertTrue(any(endpoint == "userinfo" for endpoint, _ in self.state.oauth))
        # no address was typed: the sign-ins carried no login hint
        self.assertFalse(any(f.get("login_hint") for endpoint, f in self.state.oauth if endpoint == "authorize"))

    def test_smtp_send(self):
        out, _, _ = self.run_script("_smtp_send.ls")
        self.assertIn("rejected [\"nobody@example.org\"]", out)
        self.assertEqual(len(self.state.sent), 1)
        sender, recipients, body = self.state.sent[0]
        self.assertEqual(sender, "ada@example.org")
        self.assertEqual(recipients, ["bob@example.org"])
        self.assertEqual(body, b"From: ada@example.org\r\nTo: bob@example.org\r\nSubject: hello\r\n\r\n"
                               b"Hi Bob,\r\n.a line starting with a dot\r\n")

    def test_sync_store(self):
        store = TEMP / "store"
        shutil.rmtree(store, ignore_errors=True)
        out, _, _ = self.run_script("_sync.ls")
        self.assertIn("box Entw\u00fcrfe drafts true", out)
        self.assertIn("box [Fake] null false", out)
        self.assertIn("index 1700000001 5 4", out)
        self.assertIn('entry 2 ["\\\\Seen", "\\\\Flagged"] caf\u00e9 \u00c9lise b01-1700000001-2', out)
        self.assertEqual((store / "messages/03/b01-1700000001-3.eml").read_bytes(), fake_mail.HOSTILE)
        self.assertEqual((store / "messages/04/b01-1700000001-4.eml").read_bytes(), fake_mail.BIG)
        self.assertTrue((store / "messages/03/b01-1700000001-3.txt").read_text().startswith(
            "mallory@example.org\ntricks\n"))
        # the large body was spilled into incoming/ and moved into place
        self.assertEqual(list((store / "incoming").iterdir()), [])
        # the server changes: UID 1 unread, UID 2 expunged, UID 5 delivered
        inbox = self.state.mailboxes["INBOX"]
        inbox[0] = (1, [], inbox[0][2])
        del inbox[1]
        inbox.append((5, [b"\\Answered"], b"From: Bob <bob@example.com>\r\nSubject: five\r\n\r\nnew\r\n"))
        out, _, _ = self.run_script("_sync.ls")
        self.assertIn("index 1700000001 6 4", out)
        self.assertIn("entry 1 [] one Ada Lovelace", out)
        self.assertNotIn("entry 2 ", out)
        self.assertIn('entry 5 ["\\\\Answered"] five Bob b01-1700000001-5', out)
        # a new UIDVALIDITY starts the index over
        self.state.uidvalidity += 1
        out, _, _ = self.run_script("_sync.ls")
        self.assertIn("index 1700000002 6 4", out)
        self.assertIn("b01-1700000002-5", out)
        self.assertIn("synced", out)

    def test_import_needs_alias(self):
        out, err, code = self.run_script("_import_global.ls", expect_ok=False)
        self.assertNotEqual(code, 0)
        self.assertIn("import lambda.io.mail needs an alias", out + err)

    def test_import_secret_needs_alias(self):
        out, err, code = self.run_script("_import_secret_global.ls", expect_ok=False)
        self.assertNotEqual(code, 0)
        self.assertIn("import lambda.io.secret needs an alias, as in 'import secret: lambda.io.secret'", out + err)


class SecretTests(unittest.TestCase):
    """lambda.io.secret needs no server: it talks to the OS secret store."""

    run_script = MailTests.run_script

    @unittest.skipUnless(sys.platform == "darwin", "the Keychain backend is macOS only (MQ7)")
    def test_keychain_round_trip(self):
        out, _, _ = self.run_script("_secret.ls")
        self.assertEqual(out.splitlines()[:7], [
            "absent null", "stored app password one", "replaced pässword two ✓", "other null",
            "deleted true", "again false", "gone null"])
        self.assertIn("refused: io.secret.set: the value must be text", out)
        self.assertIn("refused: io.secret.get: the account must be non-empty text without NUL", out)

    @unittest.skipIf(sys.platform == "darwin", "macOS has a backend")
    def test_unsupported_platform(self):
        out, err, code = self.run_script("_secret.ls", expect_ok=False)
        self.assertNotEqual(code, 0)
        self.assertIn("no secret store on this platform", out + err)


class DemoWindowTests(unittest.TestCase):
    """The mail window (test/demo/mail) replayed headless against the fake servers."""

    def replay(self, name, profile, setup=None, prepare=None):
        """Runs tests/<name>.json against fresh servers with a fresh profile; returns
        the servers' state and the profile folder. The password goes to the Keychain
        under the profile's own service, removed before and after with the security
        tool, which needs no Keychain prompt."""
        state, servers = fake_mail.start_all()
        if setup:
            setup(state)
        root = TEMP / profile
        forget = ["security", "delete-generic-password", "-s", f"Lambda Mail ({root})", "-a", "ada-fake-test"]
        try:
            TEMP.mkdir(parents=True, exist_ok=True)
            shutil.rmtree(root, ignore_errors=True)
            subprocess.run(forget, capture_output=True)
            if prepare:
                root.mkdir(parents=True)
                prepare(root)
            replay = (ROOT / f"test/demo/mail/tests/{name}.json").read_text()
            for port_name, server in servers.items():
                replay = replay.replace("{%s_PORT}" % port_name.upper(), str(server.port))
            (TEMP / f"{name}.json").write_text(replay)
            result = TEMP / f"{name}_result.json"
            result.unlink(missing_ok=True)
            process = subprocess.run([str(EXE), "view", "test/demo/mail/mail.ls", "--headless",
                                      "--event-file", str(TEMP / f"{name}.json"), "--event-result", str(result)],
                                     cwd=ROOT, env=dict(os.environ, MAIL_DEMO_ROOT=str(root)),
                                     capture_output=True, timeout=180)
            report = json.loads(result.read_text()) if result.exists() else {}
            self.assertEqual(report.get("result"), "PASS",
                             f"replay {report}\n{process.stderr.decode(errors='replace')[-4000:]}")
            # the profile holds the account and the mail, never the password (ML6);
            # oauth.mark is the app's registration, which the person writes
            stored = [p for p in root.rglob("*") if p.is_file() and p.name != "oauth.mark"]
            self.assertFalse([p for p in stored if fake_mail.PASSWORD.encode() in p.read_bytes()])
            return state, [p.read_bytes() for p in stored if p.suffix == ".eml"]
        finally:
            subprocess.run(forget, capture_output=True)
            for server in servers.values():
                server.stop()

    @unittest.skipUnless(sys.platform == "darwin", "the demo keeps passwords in the Keychain (macOS only, MQ7)")
    def test_demo_signin_sheet(self):
        # an app registration for Google turns the sheet's Gmail sign-in to OAuth (MQ4)
        self.replay("mail_signin_sheet", "demo-signin", prepare=lambda root: (root / "oauth.mark").write_text(
            '{google: {client_id: "lambda-test.apps.googleusercontent.com", client_secret: "s3cret"}}'))

    @unittest.skipUnless(sys.platform == "darwin", "the demo keeps passwords in the Keychain (macOS only, MQ7)")
    def test_demo_google_setup(self):
        # without a Google client, Login with Gmail asks for one and keeps it in oauth.mark
        self.replay("mail_google_setup", "demo-google")
        saved = (TEMP / "demo-google" / "oauth.mark").read_text()
        self.assertIn('client_id: "1234-abc.apps.googleusercontent.com"', saved)
        self.assertIn('client_secret: "GOCSPX-fake"', saved)

    @unittest.skipUnless(sys.platform == "darwin", "the demo keeps passwords in the Keychain (macOS only, MQ7)")
    def test_demo_google_fix(self):
        # a client saved with stray pasted characters: Google would answer "OAuth client was not found"
        def bad_client(root):
            (root / "oauth.mark").write_text(
                '{google: {client_id: "- 1234-abc.apps.googleusercontent.com", client_secret: "- GOCSPX-fake"}}')
        self.replay("mail_google_fix", "demo-google-fix", prepare=bad_client)
        saved = (TEMP / "demo-google-fix" / "oauth.mark").read_text()
        self.assertIn('client_id: "1234-abc.apps.googleusercontent.com"', saved)
        self.assertIn('client_secret: "GOCSPX-fake"', saved)

    @unittest.skipUnless(sys.platform == "darwin", "the demo keeps passwords in the Keychain (macOS only, MQ7)")
    def test_demo_window(self):
        _, messages = self.replay("mail_window", "demo",
                                  lambda state: state.mailboxes["INBOX"].append((5, [], fake_mail.HTML_MAIL)))
        self.assertIn(fake_mail.HTML_MAIL, messages)
        self.assertIn(b"ada-fake-test", (TEMP / "demo" / "accounts.mark").read_bytes())

    @unittest.skipUnless(sys.platform == "darwin", "the demo keeps passwords in the Keychain (macOS only, MQ7)")
    def test_demo_mailbox(self):
        # the demo account never connects: its mail is seeded in the profile
        state, messages = self.replay("mail_window_demo", "demo-mailbox")
        self.assertEqual(state.lines, [])
        # 330 seeded, the sent reply, one Get Mail arrival, the draft sent from Drafts (its draft erased)
        self.assertEqual(len(messages), 333)
        self.assertIn(b"protocol: \"demo\"", (TEMP / "demo-mailbox" / "accounts.mark").read_bytes())

    @unittest.skipUnless(sys.platform == "darwin", "the demo keeps passwords in the Keychain (macOS only, MQ7)")
    def test_demo_window_pop3(self):
        state, messages = self.replay("mail_window_pop3", "demo-pop3")
        # the replay ends by removing the account: from the list and from the Keychain
        self.assertNotIn(b"ada-fake-test", (TEMP / "demo-pop3" / "accounts.mark").read_bytes())
        found = subprocess.run(["security", "find-generic-password", "-s", f"Lambda Mail ({TEMP / 'demo-pop3'})",
                                "-a", "ada-fake-test"], capture_output=True)
        self.assertNotEqual(found.returncode, 0)
        # each message downloaded once, byte for byte, and none deleted (ML9)
        self.assertEqual(sorted(messages), sorted(body for _, body in state.pop3))
        commands = [line.split(b" ")[0] for line in state.lines]
        self.assertEqual(commands.count(b"RETR"), 2)
        self.assertNotIn(b"DELE", commands)


if __name__ == "__main__":
    unittest.main()
