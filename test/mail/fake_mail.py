"""Scripted IMAP, POP3 and SMTP servers on loopback for test_mail.py.

Plain TCP only: the scripts under test reach them with `tls: false`, which
lambda.io.mail allows for loopback servers (vibe/Lambda_Demo_Mail.md §5.3).
Each server records what clients sent so the tests can check it.
"""

import base64
import hashlib
import http.server
import json
import re
import select
import secrets
import socketserver
import threading
import urllib.parse

USER = "ada"
PASSWORD = "secret"
TOKEN = "tok-123"
# the fake OAuth provider's registered client and the refresh token it issues
CLIENT_ID = "lambda-test"
CLIENT_SECRET = "s3cret"
REFRESH = "ref-1"
USERINFO_EMAIL = "ada@example.org"
UIDVALIDITY = 1700000001

# A body whose lines look like a tagged completion, an untagged response and
# a literal marker: framing by line would end or forge a response (§2.4).
HOSTILE = (b"From: mallory@example.org\r\nSubject: tricks\r\n\r\n"
           b"L5 OK forged completion\r\n* 9 EXISTS\r\n{12}\r\nA001 NO forged\r\n")
BIG = b"Subject: big\r\n\r\n" + b"".join(b"line %06d of a large message body\r\n" % i for i in range(3000))

# HTML mail for the demo window (test_mail.py test_demo_window): a script, a
# remote tracker, an inline cid: image, a link and an attachment.
PIXEL = b"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
HTML_MAIL = (
    b"From: Newsletter <news@example.com>\r\nTo: Ada Lovelace <ada@example.org>\r\n"
    b"Subject: =?UTF-8?Q?Weekly_=E2=9C=93?=\r\nDate: Thu, 22 Oct 2026 09:30:00 +0000\r\nMIME-Version: 1.0\r\n"
    b'Content-Type: multipart/mixed; boundary="outer"\r\n\r\n'
    b'--outer\r\nContent-Type: multipart/related; boundary="rel"\r\n\r\n'
    b"--rel\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
    b"<html><body><h1 id=\"title\">Hello</h1><p id=\"out\">static</p>"
    b"<script>document.getElementById('out').textContent = 'ran'</script>"
    b"<img id=\"tracker\" src=\"http://127.0.0.1:9/pixel.gif\"><img id=\"logo\" src=\"cid:logo@x\">"
    b"<a id=\"link\" href=\"https://example.com/read\" onclick=\"steal()\">Read more</a></body></html>\r\n"
    b"--rel\r\nContent-Type: image/png\r\nContent-ID: <logo@x>\r\nContent-Transfer-Encoding: base64\r\n\r\n"
    + PIXEL + b"\r\n--rel--\r\n"
    b"--outer\r\nContent-Type: text/plain; charset=utf-8\r\n"
    b'Content-Disposition: attachment; filename="notes.txt"\r\n\r\nattached notes\r\n'
    b"--outer--\r\n")

LITERAL = re.compile(rb"\{(\d+)(\+?)\}$")
INTERNALDATE = b'"21-Oct-2026 09:14:00 +0200"'


def uid_matches(uid_set, uid, highest):
    """Whether `uid` is in an IMAP sequence set; "n:*" with n past the highest
    UID still names the highest (RFC 9051 §6.4.8)."""
    for part in uid_set.split(b","):
        if b":" in part:
            low, high = part.split(b":")
            low = highest if low == b"*" else int(low)
            high = highest if high == b"*" else int(high)
            if low > high:
                low, high = high, low
            if low <= uid <= high:
                return True
        elif (highest if part == b"*" else int(part)) == uid:
            return True
    return False


def header_fields(message, names):
    """The header lines named in `names`, as BODY[HEADER.FIELDS (...)] returns them."""
    head = message.split(b"\r\n\r\n", 1)[0]
    kept, keep = [], False
    for line in head.split(b"\r\n"):
        if line[:1] in (b" ", b"\t"):
            if keep:
                kept.append(line)
            continue
        keep = line.split(b":", 1)[0].strip().upper() in names
        if keep:
            kept.append(line)
    return b"\r\n".join(kept) + b"\r\n\r\n"


def credentials_ok(user, password=None, token=None):
    return user == USER and (password == PASSWORD or token == TOKEN)


def sasl_identity(mechanism, payload):
    """(user, password, token) from a decoded SASL initial response."""
    if mechanism == "PLAIN":
        parts = payload.split(b"\0")
        if len(parts) == 3:
            return parts[1].decode(), parts[2].decode(), None
    fields = dict(part.split(b"=", 1) for part in payload.replace(b",", b"\x01").split(b"\x01") if b"=" in part)
    user = fields.get(b"user") or fields.get(b"a")
    auth = fields.get(b"auth", b"")
    token = auth[7:].decode() if auth.startswith(b"Bearer ") else None
    return (user.decode() if user else None), None, token


class LineHandler(socketserver.StreamRequestHandler):
    def send(self, *lines):
        for line in lines:
            self.wfile.write((line if isinstance(line, bytes) else line.encode()) + b"\r\n")
        self.wfile.flush()

    def read_line(self):
        try:
            line = self.rfile.readline()
        except ConnectionResetError:
            # a client process that exits with its session open resets the socket
            return None
        if not line:
            return None
        return line.rstrip(b"\r\n")


class ImapHandler(LineHandler):
    def read_command(self):
        """A command line with its literals replaced by their bytes."""
        line = self.read_line()
        if line is None:
            return None, []
        literals = []
        while True:
            match = LITERAL.search(line)
            if not match:
                return line, literals
            size, nonsync = int(match.group(1)), match.group(2)
            if not nonsync:
                self.send("+ Ready for literal data")
            literals.append(self.rfile.read(size))
            rest = self.read_line() or b""
            line = line[:match.start()] + b"\x00LIT\x00" + rest

    def handle(self):
        state = self.server.state
        selected = None
        idles = 0
        self.send("* OK [CAPABILITY IMAP4rev1 LITERAL+ IDLE UIDPLUS AUTH=PLAIN AUTH=XOAUTH2 "
                  "AUTH=OAUTHBEARER SASL-IR] fake ready")
        while True:
            line, literals = self.read_command()
            if line is None:
                return
            state.record(line)
            tag, _, rest = line.partition(b" ")
            command, _, args = rest.partition(b" ")
            command = command.upper()
            t = tag.decode()
            if command == b"CAPABILITY":
                self.send("* CAPABILITY IMAP4rev1 LITERAL+ IDLE UIDPLUS AUTH=PLAIN AUTH=XOAUTH2 "
                          "AUTH=OAUTHBEARER SASL-IR", f"{t} OK CAPABILITY completed")
            elif command == b"AUTHENTICATE":
                mechanism, _, initial = args.partition(b" ")
                if not initial:
                    self.send("+ ")
                    initial = self.read_line() or b""
                user, password, token = sasl_identity(mechanism.decode().upper(), base64.b64decode(initial))
                state.logins.append((mechanism.decode().upper(), user, password, token))
                if credentials_ok(user, password, token):
                    self.send(f"{t} OK [CAPABILITY IMAP4rev1 LITERAL+ IDLE UIDPLUS] Logged in")
                else:
                    self.send(f"{t} NO [AUTHENTICATIONFAILED] Invalid credentials")
            elif command == b"LOGIN":
                user, password = [p.strip(b'"').decode() for p in args.split(b" ", 1)]
                state.logins.append(("LOGIN", user, password, None))
                self.send(f"{t} OK Logged in" if credentials_ok(user, password)
                          else f"{t} NO [AUTHENTICATIONFAILED] Invalid credentials")
            elif command in (b"SELECT", b"EXAMINE"):
                name = args.strip(b'"').decode()
                if name not in state.mailboxes:
                    self.send(f"{t} NO [TRYCREATE] Mailbox doesn't exist")
                    continue
                selected = name
                count = len(state.mailboxes[name])
                uidnext = (state.mailboxes[name][-1][0] + 1) if count else 1
                self.send("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)",
                          f"* {count} EXISTS",
                          f"* OK [UIDVALIDITY {state.uidvalidity}] UIDs valid",
                          f"* OK [UIDNEXT {uidnext}] Predicted next UID",
                          f"{t} OK [READ-WRITE] SELECT completed")
            elif command == b"UID" and args.upper().startswith(b"FETCH "):
                _, uid_set, items = args.split(b" ", 2)
                messages = state.mailboxes.get(selected, [])
                highest = messages[-1][0] if messages else 0
                upper = items.upper()
                fields = re.search(rb"BODY\.PEEK\[HEADER\.FIELDS \(([^)]*)\)\]", upper)
                for seq, (uid, flags, body) in enumerate(messages, 1):
                    if not uid_matches(uid_set, uid, highest):
                        continue
                    parts = [b"UID %d" % uid]
                    if b"FLAGS" in upper:
                        parts.append(b"FLAGS (" + b" ".join(flags) + b")")
                    if b"RFC822.SIZE" in upper:
                        parts.append(b"RFC822.SIZE %d" % len(body))
                    if b"INTERNALDATE" in upper:
                        parts.append(b"INTERNALDATE " + INTERNALDATE)
                    if fields:
                        head = header_fields(body, fields.group(1).split())
                        parts.append(b"BODY[HEADER.FIELDS (" + fields.group(1) + b")] {%d}\r\n" % len(head) + head)
                    if b"BODY.PEEK[]" in upper:
                        parts.append(b"BODY[] {%d}\r\n" % len(body) + body)
                    self.wfile.write(b"* %d FETCH (" % seq + b" ".join(parts) + b")\r\n")
                self.send(f"{t} OK FETCH completed")
            elif command == b"LIST":
                for name, flags in state.listing:
                    self.send(f'* LIST ({flags}) "/" "{name}"')
                self.send(f"{t} OK LIST completed")
            elif command == b"APPEND":
                name = args.split(b" ", 1)[0].strip(b'"').decode()
                box = state.mailboxes.setdefault(name, [])
                uid = len(box) + 1
                box.append((uid, [b"\\Seen"], literals[0] if literals else b""))
                state.appended.append((name, args, literals))
                self.send(f"{t} OK [APPENDUID {UIDVALIDITY} {uid}] APPEND completed")
            elif command == b"IDLE":
                idles += 1
                self.send("+ idling")
                if idles == 1:
                    # the first IDLE of a connection sees a new message arrive;
                    # select, not a socket timeout, which would spoil rfile
                    readable, _, _ = select.select([self.connection], [], [], 0.3)
                    if not readable:
                        self.send("* 5 EXISTS")
                done = self.read_line()
                state.record(done or b"")
                if done is None:
                    return
                if done.upper() != b"DONE":
                    self.send(f"{t} BAD Expected DONE")
                    continue
                self.send(f"{t} OK IDLE terminated")
            elif command == b"NOOP":
                self.send(f"{t} OK NOOP completed")
            elif command == b"LOGOUT":
                self.send("* BYE fake logging out", f"{t} OK LOGOUT completed")
                return
            else:
                self.send(f"{t} BAD Unknown command")


class Pop3Handler(LineHandler):
    def handle(self):
        state = self.server.state
        user = None
        self.send("+OK fake POP3 ready")
        while True:
            line = self.read_line()
            if line is None:
                return
            state.record(line)
            command, _, args = line.partition(b" ")
            command = command.upper()
            messages = state.pop3
            if command == b"CAPA":
                self.send("+OK Capability list follows", "USER", "UIDL", "TOP", ".")
            elif command == b"USER":
                user = args.decode()
                self.send("+OK")
            elif command == b"PASS":
                ok = credentials_ok(user, args.decode())
                state.logins.append(("USER", user, args.decode(), None))
                self.send("+OK logged in" if ok else "-ERR [AUTH] invalid login")
            elif command == b"NOOP":
                self.send("+OK")
            elif command == b"STAT":
                self.send(f"+OK {len(messages)} {sum(len(m[1]) for m in messages)}")
            elif command == b"UIDL" and not args:
                self.send("+OK", *[f"{i} {uid}" for i, (uid, _) in enumerate(messages, 1)], ".")
            elif command == b"RETR":
                index = int(args) - 1
                if not 0 <= index < len(messages):
                    self.send("-ERR no such message")
                    continue
                body = messages[index][1]
                # the body's own final CRLF ends its last line, not an extra one
                lines = (body[:-2] if body.endswith(b"\r\n") else body).split(b"\r\n")
                stuffed = [(b"." + l if l.startswith(b".") else l) for l in lines]
                self.send(f"+OK {len(body)} octets", *stuffed, ".")
            elif command == b"DELE":
                self.send("-ERR no such message")
            elif command == b"QUIT":
                self.send("+OK bye")
                return
            else:
                self.send("-ERR unknown command")


class SmtpHandler(LineHandler):
    def handle(self):
        state = self.server.state
        envelope = {"from": None, "to": []}
        self.send("220 fake ESMTP ready")
        while True:
            line = self.read_line()
            if line is None:
                return
            state.record(line)
            word = line.split(b" ", 1)[0].upper()
            if word in (b"EHLO", b"HELO"):
                self.send("250-fake greets you", "250-AUTH PLAIN", "250 8BITMIME")
            elif word == b"AUTH":
                parts = line.split(b" ")
                initial = parts[2] if len(parts) > 2 else None
                if initial is None:
                    self.send("334 ")
                    initial = self.read_line() or b""
                user, password, token = sasl_identity("PLAIN", base64.b64decode(initial))
                state.logins.append(("PLAIN", user, password, token))
                if credentials_ok(user, password, token):
                    self.send("235 2.7.0 Authentication successful")
                else:
                    # a multi-line refusal, as Gmail sends it
                    self.send("535-5.7.8 Username and Password not accepted. For more information, go to",
                              "535 5.7.8 https://support.example.com/BadCredentials")
            elif word == b"MAIL":
                envelope = {"from": line[10:].strip(b"<>").decode(), "to": []}
                self.send("250 2.1.0 OK")
            elif word == b"RCPT":
                address = line[8:].strip(b"<>").decode()
                if "nobody" in address:
                    self.send("550 5.1.1 No such user")
                else:
                    envelope["to"].append(address)
                    self.send("250 2.1.5 OK")
            elif word == b"DATA":
                self.send("354 End data with <CR><LF>.<CR><LF>")
                lines = []
                while True:
                    data = self.read_line()
                    if data is None or data == b".":
                        break
                    lines.append(data[1:] if data.startswith(b"..") else data)
                state.sent.append((envelope["from"], list(envelope["to"]), b"\r\n".join(lines) + b"\r\n"))
                self.send("250 2.0.0 queued as 42")
            elif word == b"RSET":
                self.send("250 2.0.0 OK")
            elif word == b"QUIT":
                self.send("221 2.0.0 bye")
                return
            else:
                self.send("502 5.5.2 unknown command")


class MailState:
    """What every fake server holds and observed."""

    def __init__(self):
        self.lock = threading.Lock()
        self.lines = []
        self.logins = []
        self.appended = []
        self.sent = []
        self.uidvalidity = UIDVALIDITY
        self.mailboxes = {
            "INBOX": [
                (1, [b"\\Seen"], b"From: Ada Lovelace <ada@example.org>\r\nSubject: one\r\n\r\nfirst\r\n"),
                (2, [b"\\Seen", b"\\Flagged"],
                 b"From: =?UTF-8?B?w4lsaXNl?= <elise@example.fr>\r\nSubject: =?ISO-8859-1?Q?caf=E9?=\r\n\r\nsecond\r\n"),
                (3, [], HOSTILE),
                (4, [], BIG),
            ],
            "Copies": [],
            "Entw&APw-rfe": [],
        }
        self.listing = [("INBOX", "\\HasNoChildren"), ("Copies", "\\HasNoChildren"),
                        ("Entw&APw-rfe", "\\HasNoChildren \\Drafts"), ("[Fake]", "\\Noselect \\HasChildren")]
        self.codes = {}      # OAuth code → the authorization request it answers
        self.oauth = []      # (endpoint, fields) the OAuth provider received
        self.pop3 = [("uid-alpha", b"Subject: pop\r\n\r\nhello\r\n.hidden dot line\r\n"),
                     ("uid-beta", b"Subject: second\r\n\r\nbye\r\n")]

    def record(self, line):
        with self.lock:
            self.lines.append(line)


def pkce_challenge(verifier):
    """RFC 7636 §4.2: BASE64URL(SHA256(verifier)) without padding."""
    digest = hashlib.sha256(verifier.encode()).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode()


class OAuthHandler(http.server.BaseHTTPRequestHandler):
    """A provider's authorization and token endpoints (RFC 6749, RFC 7636).
    /authorize stands in for the sign-in page: it redirects at once, with a
    code, or with error=access_denied for the login_hint "refuse"."""

    def log_message(self, *args):
        pass

    def reply_json(self, status, data):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        state = self.server.state
        url = urllib.parse.urlsplit(self.path)
        if url.path == "/userinfo":
            # OpenID Connect userinfo: who the access token was issued to
            state.oauth.append(("userinfo", {"authorization": self.headers.get("Authorization", "")}))
            if self.headers.get("Authorization") == "Bearer " + TOKEN:
                self.reply_json(200, {"sub": "1", "email": USERINFO_EMAIL, "email_verified": True, "name": "Ada Lovelace"})
            else:
                self.reply_json(401, {"error": "invalid_token"})
            return
        query = dict(urllib.parse.parse_qsl(url.query, keep_blank_values=True))
        state.oauth.append(("authorize", query))
        if url.path != "/authorize" or query.get("client_id") != CLIENT_ID or \
                query.get("code_challenge_method") != "S256" or not query.get("redirect_uri", "").startswith("http://127.0.0.1:"):
            self.reply_json(400, {"error": "invalid_request"})
            return
        if query.get("login_hint") == "refuse":
            answer = {"error": "access_denied", "error_description": "The user said no", "state": query.get("state")}
        else:
            code = secrets.token_urlsafe(12)
            state.codes[code] = query
            answer = {"code": code, "state": query.get("state")}
        self.send_response(302)
        self.send_header("Location", query["redirect_uri"] + "?" + urllib.parse.urlencode(answer))
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_POST(self):
        state = self.server.state
        length = int(self.headers.get("Content-Length", "0"))
        fields = dict(urllib.parse.parse_qsl(self.rfile.read(length).decode()))
        state.oauth.append(("token", fields))
        if fields.get("client_id") != CLIENT_ID or fields.get("client_secret") != CLIENT_SECRET:
            self.reply_json(401, {"error": "invalid_client"})
            return
        if fields.get("grant_type") == "authorization_code":
            request = state.codes.pop(fields.get("code"), None)
            if not request or request["redirect_uri"] != fields.get("redirect_uri") or \
                    pkce_challenge(fields.get("code_verifier", "")) != request["code_challenge"]:
                self.reply_json(400, {"error": "invalid_grant", "error_description": "Bad code or verifier"})
                return
            self.reply_json(200, {"access_token": TOKEN, "refresh_token": REFRESH, "expires_in": 3600,
                                  "token_type": "Bearer", "scope": request.get("scope")})
        elif fields.get("grant_type") == "refresh_token" and fields.get("refresh_token") == REFRESH:
            self.reply_json(200, {"access_token": TOKEN, "expires_in": 3600, "token_type": "Bearer"})
        else:
            self.reply_json(400, {"error": "invalid_grant", "error_description": "Token has been expired or revoked."})


class HttpServer(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, handler, state):
        super().__init__(("127.0.0.1", 0), handler)
        self.state = state
        self.thread = threading.Thread(target=self.serve_forever, daemon=True)
        self.thread.start()

    @property
    def port(self):
        return self.server_address[1]

    def stop(self):
        self.shutdown()
        self.server_close()


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, handler, state):
        super().__init__(("127.0.0.1", 0), handler)
        self.state = state
        self.thread = threading.Thread(target=self.serve_forever, daemon=True)
        self.thread.start()

    @property
    def port(self):
        return self.server_address[1]

    def stop(self):
        self.shutdown()
        self.server_close()


def start_all():
    state = MailState()
    return state, {"imap": Server(ImapHandler, state),
                   "pop3": Server(Pop3Handler, state),
                   "smtp": Server(SmtpHandler, state),
                   "oauth": HttpServer(OAuthHandler, state)}
