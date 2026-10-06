#!/usr/bin/env python3
"""Real POSIX interaction checks for the Lambda terminal template host."""

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import termios
import time


PROMPT = "λ> ".encode()


class ReplPty:
    def __init__(self, executable, columns=80):
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 24, columns, 0, 0))
        self.original_mode = termios.tcgetattr(self.slave)
        self.output = bytearray()
        env = dict(os.environ, LANG="en_US.UTF-8")
        self.process = subprocess.Popen(
            [executable, "--no-log"], stdin=self.slave, stdout=self.slave,
            stderr=self.slave, close_fds=True, env=env)

    def expect(self, marker, start=0, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            found = self.output.find(marker, start)
            if found >= 0:
                return found + len(marker)
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    self.output.extend(os.read(self.master, 4096))
                except OSError:
                    break
        raise AssertionError(f"missing {marker!r}; tail={bytes(self.output[-400:])!r}")

    def send(self, data):
        os.write(self.master, data)

    def finish(self):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and self.process.poll() is None:
            if select.select([self.master], [], [], 0.1)[0]:
                try:
                    self.output.extend(os.read(self.master, 4096))
                except OSError:
                    break
        if self.process.poll() is None:
            self.process.kill()
            raise AssertionError(f"REPL failed to exit; tail={bytes(self.output[-400:])!r}")
        assert self.process.returncode == 0, self.process.returncode
        assert termios.tcgetattr(self.slave) == self.original_mode, "termios not restored"
        os.close(self.master)
        os.close(self.slave)


def check_edit_history(executable):
    pty_session = ReplPty(executable)
    position = pty_session.expect(PROMPT)
    pty_session.send(b"12\x1b[D3\r")
    position = pty_session.expect(b"\r\n132\r\n", position)
    position = pty_session.expect(PROMPT, position)
    pty_session.send(b"\x1b[A\r")
    position = pty_session.expect(b"\r\n132\r\n", position)
    pty_session.expect(PROMPT, position)
    pty_session.send(b"quit\r")
    pty_session.finish()


def check_wrapping(executable):
    pty_session = ReplPty(executable, columns=8)
    position = pty_session.expect(PROMPT)
    pty_session.send(b'"123456789"\r')
    position = pty_session.expect(b'"123456789"\r\n', position)
    pty_session.expect(PROMPT, position)
    pty_session.send(b"quit\r")
    pty_session.finish()
    assert b"\x1b[1A" in pty_session.output, "wrapped frame was not repainted"


def check_kill_yank(executable):
    pty_session = ReplPty(executable)
    position = pty_session.expect(PROMPT)
    pty_session.send(b"12\x01\x0b\x19\r")
    position = pty_session.expect(b"\r\n12\r\n", position)
    pty_session.expect(PROMPT, position)
    pty_session.send(b"quit\r")
    pty_session.finish()


def check_bracketed_paste(executable):
    pty_session = ReplPty(executable)
    position = pty_session.expect(PROMPT)
    pty_session.send(b"\x1b[200~1\t+2\x1b[201~\r")
    position = pty_session.expect(b"\r\n3\r\n", position)
    assert b"1    +2" in pty_session.output, "paste tab was not inserted"
    assert b"\x1b[?2004h" in pty_session.output
    assert b"\x1b[?2004l" in pty_session.output
    pty_session.expect(PROMPT, position)
    pty_session.send(b"quit\r")
    pty_session.finish()

    undo_session = ReplPty(executable)
    position = undo_session.expect(PROMPT)
    undo_session.send(b"\x1b[200~12\x1b[201~\x1a3\r")
    position = undo_session.expect(b"\r\n3\r\n", position)
    undo_session.expect(PROMPT, position)
    undo_session.send(b"quit\r")
    undo_session.finish()

    tab_session = ReplPty(executable)
    position = tab_session.expect(PROMPT)
    tab_session.send(b"1\t+2\r")
    position = tab_session.expect(b"\r\n3\r\n", position)
    assert b"1    +2" in tab_session.output, "plain Tab was not inserted"
    tab_session.expect(PROMPT, position)
    tab_session.send(b"quit\r")
    tab_session.finish()


def check_resize(executable):
    pty_session = ReplPty(executable, columns=8)
    position = pty_session.expect(PROMPT)
    pty_session.send(b'"123456789"')
    pty_session.expect(b'56789"', position)
    fcntl.ioctl(pty_session.slave, termios.TIOCSWINSZ,
                struct.pack("HHHH", 24, 20, 0, 0))
    os.kill(pty_session.process.pid, signal.SIGWINCH)
    position = pty_session.expect(PROMPT + b'"123456789"',
                                  len(pty_session.output))
    pty_session.send(b"\r")
    position = pty_session.expect(b'"123456789"\r\n', position)
    pty_session.expect(PROMPT, position)
    pty_session.send(b"quit\r")
    pty_session.finish()


def check_signal_restore(executable):
    pty_session = ReplPty(executable)
    pty_session.expect(PROMPT)
    pty_session.send(b"unfinished")
    os.kill(pty_session.process.pid, signal.SIGTERM)
    pty_session.finish()


def check_control_d(executable):
    pty_session = ReplPty(executable)
    position = pty_session.expect(PROMPT)
    pty_session.send(b"12\x1b[D\x04\r")
    position = pty_session.expect(b"\r\n1\r\n", position)
    pty_session.expect(PROMPT, position)
    pty_session.send(b"\x04")
    pty_session.finish()


def check_interrupt(executable):
    pty_session = ReplPty(executable)
    position = pty_session.expect(PROMPT)
    pty_session.send(b"123\x03")
    position = pty_session.expect(b"\r\n", position)
    position = pty_session.expect(PROMPT, position)
    pty_session.send(b"4\r")
    position = pty_session.expect(b"\r\n4\r\n", position)
    position = pty_session.expect(PROMPT, position)
    pty_session.send(b"let x = (\r")
    position = pty_session.expect(b".. ", position)
    pty_session.send(b"\x03")
    position = pty_session.expect(b"\r\n", position)
    position = pty_session.expect(PROMPT, position)
    pty_session.send(b"5\r")
    position = pty_session.expect(b"\r\n5\r\n", position)
    pty_session.expect(PROMPT, position)
    pty_session.send(b"quit\r")
    pty_session.finish()


def check_redirected(executable):
    for source, expected in ((b"12\nquit\n", b"> 12\n"), (b"7", b"> 7\n")):
        result = subprocess.run([executable, "--no-log"], input=source,
                                capture_output=True, timeout=15)
        assert result.returncode == 0, result.stderr
        assert expected in result.stdout, result.stdout
        assert b"\x1b[" not in result.stdout, "VT bytes leaked to redirected output"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lambda", dest="executable", default="./lambda.exe")
    args = parser.parse_args()
    if os.name == "nt":
        raise SystemExit("POSIX PTY fixture requires macOS or Linux")
    check_edit_history(args.executable)
    check_wrapping(args.executable)
    check_kill_yank(args.executable)
    check_bracketed_paste(args.executable)
    check_resize(args.executable)
    check_signal_restore(args.executable)
    check_control_d(args.executable)
    check_interrupt(args.executable)
    check_redirected(args.executable)
    print("io_readline_pty: passed")


if __name__ == "__main__":
    main()
