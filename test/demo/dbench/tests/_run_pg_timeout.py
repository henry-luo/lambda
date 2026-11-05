#!/usr/bin/env python3
"""Verify libpq honors connect_timeout when a peer stalls during authentication."""
import os
from pathlib import Path
import socket
import subprocess
import threading
import time

root = Path(__file__).resolve().parents[4]
listener = socket.socket()
listener.bind(("127.0.0.1", 0))
listener.listen(1)
listener.settimeout(8)
port = listener.getsockname()[1]


def stall():
    try:
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(8)
            while connection.recv(4096):
                pass
    except (OSError, TimeoutError):
        pass


server = threading.Thread(target=stall)
server.start()
environment = os.environ.copy()
environment["TMPDIR"] = str(root / "temp")
environment["DBENCH_TEST_URI"] = (
    f"postgresql://nobody@127.0.0.1:{port}/test?sslmode=disable&connect_timeout=2")
try:
    started = time.monotonic()
    result = subprocess.run([str(root / "lambda.exe"), "run", "test/demo/dbench/tools/_pg_connect_timeout.ls",
                             "--no-log"], cwd=root, env=environment, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8, check=True)
    assert result.stdout.strip() == "true", result.stdout
    assert time.monotonic() - started < 6
finally:
    listener.close()
    server.join(timeout=9)
