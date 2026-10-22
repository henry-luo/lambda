"""Scripted IMAP, POP3 and SMTP servers on loopback for test_mail.py.

Plain TCP only: the scripts under test reach them with `tls: false`, which
lambda.io.mail allows for loopback servers (vibe/Lambda_Demo_Mail.md §5.3).
Each server records what clients sent so the tests can check it.
"""

import base64
import re
import select
import socketserver
import threading

USER = "ada"
PASSWORD = "secret"
TOKEN = "tok-123"
UIDVALIDITY = 1700000001

# A body whose lines look like a tagged completion, an untagged response and
# a literal marker: framing by line would end or forge a response (§2.4).
HOSTILE = (b"From: mallory@example.org\r\nSubject: tricks\r\n\r\n"
           b"L5 OK forged completion\r\n* 9 EXISTS\r\n{12}\r\nA001 NO forged\r\n")
BIG = b"Subject: big\r\n\r\n" + b"".join(b"line %06d of a large message body\r\n" % i for i in range(3000))

LITERAL = re.compile(rb"\{(\d+)(\+?)\}$")


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
                self.send("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)",
                          f"* {count} EXISTS",
                          f"* OK [UIDVALIDITY {UIDVALIDITY}] UIDs valid",
                          f"{t} OK [READ-WRITE] SELECT completed")
            elif command == b"UID" and args.upper().startswith(b"FETCH "):
                _, uid_set, items = args.split(b" ", 2)
                messages = state.mailboxes.get(selected, [])
                for seq, (uid, flags, body) in enumerate(messages, 1):
                    if uid_set != b"1:*" and int(uid_set) != uid:
                        continue
                    parts = [b"UID %d" % uid]
                    if b"FLAGS" in items:
                        parts.append(b"FLAGS (" + b" ".join(flags) + b")")
                    if b"BODY.PEEK[]" in items:
                        parts.append(b"BODY[] {%d}\r\n" % len(body) + body)
                    self.wfile.write(b"* %d FETCH (" % seq + b" ".join(parts) + b")\r\n")
                self.send(f"{t} OK FETCH completed")
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
                self.send("235 2.7.0 Authentication successful" if credentials_ok(user, password, token)
                          else "535 5.7.8 Authentication credentials invalid")
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
        self.mailboxes = {
            "INBOX": [
                (1, [b"\\Seen"], b"Subject: one\r\n\r\nfirst\r\n"),
                (2, [b"\\Seen", b"\\Flagged"], b"Subject: two\r\n\r\nsecond\r\n"),
                (3, [], HOSTILE),
                (4, [], BIG),
            ],
            "Copies": [],
        }
        self.pop3 = [("uid-alpha", b"Subject: pop\r\n\r\nhello\r\n.hidden dot line\r\n"),
                     ("uid-beta", b"Subject: second\r\n\r\nbye\r\n")]

    def record(self, line):
        with self.lock:
            self.lines.append(line)


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
                   "smtp": Server(SmtpHandler, state)}
