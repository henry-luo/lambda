"""A finite RFC 8489 UDP fixture checks configured STUN, not Internet traversal."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[2]
AREA = ROOT / "temp/sync-connectivity"
COOKIE = 0x2112A442


def main():
    AREA.mkdir(parents=True, exist_ok=True)
    received = []
    errors = []
    done = threading.Event()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.1)

    def serve():
        while not done.is_set():
            try:
                data, address = sock.recvfrom(4096)
            except socket.timeout:
                continue
            if len(data) < 20:
                errors.append("short STUN message")
                continue
            kind, size, cookie = struct.unpack("!HHI", data[:8])
            if kind != 1 or cookie != COOKIE or len(data) != 20 + size:
                errors.append("invalid Binding request")
                continue
            # XOR-MAPPED-ADDRESS uses the observed source, with no invented NAT mapping.
            ip = struct.unpack("!I", socket.inet_aton(address[0]))[0]
            attribute = struct.pack("!HHBBHI", 0x20, 8, 0, 1, address[1] ^ (COOKIE >> 16), ip ^ COOKIE)
            response = struct.pack("!HHI", 0x101, len(attribute), COOKIE) + data[8:20] + attribute
            sock.sendto(response, address)
            received.append(data[8:20])

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        result = subprocess.run([os.environ.get("LAMBDA_SYNC_EXE", str(ROOT / "lambda.exe")),
            "run", "--no-log", "test/sync_connectivity/_stun.ls"], cwd=ROOT, capture_output=True,
            env=dict(os.environ, LAMBDA_TEST_STUN_PORT=str(sock.getsockname()[1]), TMPDIR=str(AREA)), timeout=60)
        (AREA / "stun.stdout").write_bytes(result.stdout)
        (AREA / "stun.stderr").write_bytes(result.stderr)
        assert result.returncode == 0 and result.stdout.strip() == b"true", "native STUN candidate probe failed"
        assert received and not errors, "configured STUN was not used correctly"
        print("PASS native ICE gathers a server-reflexive candidate from actual UDP STUN", flush=True)
    finally:
        done.set()
        thread.join()
        sock.close()


if __name__ == "__main__":
    main()
