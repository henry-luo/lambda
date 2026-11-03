#!/usr/bin/env python3
"""Independent RFC 6455 peers for Lambda; artifacts stay under ./temp/."""
import base64
import hashlib
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import threading
import time
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS = ROOT / 'temp' / 'websocket-tests'
GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


def exact(sock, n):
    data = bytearray()
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise EOFError('peer closed')
        data.extend(chunk)
    return bytes(data)


def frame(payload=b'', opcode=1, final=True, masked=False):
    first = (0x80 if final else 0) | opcode
    length = len(payload)
    second = 0x80 if masked else 0
    if length < 126:
        header = bytes([first, second | length])
    elif length <= 65535:
        header = bytes([first, second | 126]) + struct.pack('!H', length)
    else:
        header = bytes([first, second | 127]) + struct.pack('!Q', length)
    if masked:
        mask = b'1234'
        return header + mask + bytes(x ^ mask[i % 4] for i, x in enumerate(payload))
    return header + payload


def read_frame(sock):
    a, b = exact(sock, 2)
    n = b & 127
    if n == 126:
        n = struct.unpack('!H', exact(sock, 2))[0]
    elif n == 127:
        n = struct.unpack('!Q', exact(sock, 8))[0]
    mask = exact(sock, 4) if b & 128 else None
    data = exact(sock, n)
    if mask:
        data = bytes(x ^ mask[i % 4] for i, x in enumerate(data))
    return a & 15, bool(b & 128), data


def handshake(sock, bad_accept=False, extra=b''):
    head = b''
    while b'\r\n\r\n' not in head:
        head += exact(sock, 1)
        if len(head) > 16384:
            raise ValueError('headers exceeded limit')
    headers = dict(line.split(b':', 1) for line in head.split(b'\r\n')[1:] if b':' in line)
    key = headers[b'Sec-WebSocket-Key'].strip()
    accept = base64.b64encode(hashlib.sha1(key + GUID.encode()).digest())
    if bad_accept:
        accept = b'wrong'
    sock.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n' + extra + b'\r\n')


class Peer:
    def __init__(self, handler, tls=None):
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.listener.settimeout(10)
        self.port = self.listener.getsockname()[1]
        self.handler = handler
        self.tls = tls
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        try:
            with self.listener:
                conn, _ = self.listener.accept()
                with conn:
                    conn.settimeout(5)
                    if self.tls:
                        with self.tls.wrap_socket(conn, server_side=True) as secured:
                            self.handler(secured)
                    else:
                        self.handler(conn)
        except BaseException as exc:
            self.error = exc

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.thread.join(10)
        if self.thread.is_alive():
            raise AssertionError('peer did not finish')
        if self.error and exc_type is None:
            raise self.error


def run_client(peer, script='_client.ls', ca=None, tier='auto'):
    env = dict(os.environ, WEBSOCKET_TEST_URL=f'{"wss" if peer.tls else "ws"}://127.0.0.1:{peer.port}/test',
               LAMBDA_EXEC_BACKEND=tier, LAMBDA_LOG_FILE=str(ARTIFACTS / 'runtime.log'))
    if ca:
        env['WEBSOCKET_TEST_CA'] = str(ca)
    result = subprocess.run([os.environ.get('LAMBDA_WEBSOCKET_EXE', str(ROOT / 'lambda.exe')), 'run', str(ROOT / 'test/websocket' / script)],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=15)
    return result


def exchange(sock):
    handshake(sock)
    opcode, masked, data = read_frame(sock)
    assert (opcode, masked, data) == (1, True, 'Hello 世界'.encode())
    # split a UTF-8 code point between fragments and interleave a ping.
    payload = 'Welcome 世界'.encode()
    sock.sendall(frame(payload[:9], final=False) + frame(b'ping', opcode=9) + frame(payload[9:], opcode=0))
    assert read_frame(sock) == (10, True, b'ping')
    sock.sendall(frame(b'\x00\xff', opcode=2) + frame(struct.pack('!H', 1000), opcode=8))
    assert read_frame(sock) == (8, True, struct.pack('!H', 1000))


class WebSocketTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ARTIFACTS.mkdir(parents=True, exist_ok=True)

    def test_independent_fragmented_ping_binary_close_across_tiers(self):
        for tier in ('interp', 'auto', 'jit'):
            with self.subTest(tier=tier), Peer(exchange) as peer:
                result = run_client(peer, tier=tier)
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                self.assertIn('["text", "Welcome 世界", "binary", true, "close", 1000]', result.stdout)

    def test_listener_upgrade_origin_and_protocol_validation(self):
        key = base64.b64encode(os.urandom(16)).decode()
        base = (f'GET /chat HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n'
                f'Connection: keep-alive\r\nConnection: upgrade\r\nSec-WebSocket-Version: 13\r\n'
                f'Sec-WebSocket-Key: {key}\r\nOrigin: http://allowed.test\r\n'
                'Sec-WebSocket-Protocol: other, chat.v1\r\n\r\n')
        cases = {
            'valid': (base, 101),
            'origin': (base.replace('http://allowed.test', 'http://untrusted.test'), 403),
            'duplicate-host': (base.replace('Host: localhost', 'Host: localhost\r\nHost: other'), 400),
            'bad-key': (base.replace(key, 'short'), 400),
            'empty-protocol': (base.replace('other, chat.v1', ''), 400),
            'trailing-comma': (base.replace('other, chat.v1', 'chat.v1,'), 400),
            'duplicate-protocol': (base.replace('other, chat.v1', 'chat.v1, chat.v1'), 400),
            'request-target': (base.replace('/chat HTTP', '/bad\ttarget HTTP'), 400),
        }
        for label, (request, status) in cases.items():
            with self.subTest(case=label):
                port_file = ARTIFACTS / f'listener-{uuid.uuid4().hex}.txt'
                env = dict(os.environ, WEBSOCKET_TEST_PORT_FILE=str(port_file))
                executable = os.environ.get('LAMBDA_WEBSOCKET_EXE', str(ROOT / 'lambda.exe'))
                process = subprocess.Popen([executable, 'run', 'test/websocket/_listener.ls'],
                    cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    deadline = time.monotonic() + 8
                    while not port_file.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(.01)
                    self.assertTrue(port_file.exists(), 'listener did not start')
                    with socket.create_connection(('127.0.0.1', int(port_file.read_text())), timeout=5) as sock:
                        sock.sendall(request.encode())
                        head = b''
                        while b'\r\n\r\n' not in head:
                            head += exact(sock, 1)
                        self.assertEqual(int(head.split(b' ')[1]), status)
                        if status == 101:
                            self.assertIn(b'Sec-WebSocket-Protocol: chat.v1\r\n', head)
                            close = struct.pack('!H', 3503) + b'complete'
                            sock.sendall(frame(close, opcode=8, masked=True))
                            self.assertEqual(read_frame(sock), (8, False, close))
                    output, error = process.communicate(timeout=8)
                    self.assertEqual(process.returncode, 0, error + output)
                    self.assertIn('["chat.v1", 3503, true]' if status == 101 else '405', output)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate()

    def test_duplex_backpressure_and_ping(self):
        def duplex(sock):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            handshake(sock)
            time.sleep(.1)
            sock.sendall(frame(b'ping', opcode=9) + frame(b'duplex'))
            opcode, masked, payload = read_frame(sock)
            self.assertEqual((opcode, masked, len(payload)), (1, True, 8388608))
            self.assertEqual(payload, b'x' * 8388608)
            self.assertEqual(read_frame(sock), (10, True, b'ping'))
            opcode, masked, payload = read_frame(sock)
            self.assertEqual((opcode, masked), (8, True))
            sock.sendall(frame(payload, opcode=8))
        with Peer(duplex) as peer:
            result = run_client(peer, '_duplex.ls')
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn('["text", "duplex"]', result.stdout)

    def test_peer_close_waits_for_pending_writer(self):
        def close_during_write(sock):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 65536)
            handshake(sock)
            time.sleep(.1)
            close = struct.pack('!H', 3504) + b'peer closing'
            sock.sendall(frame(close, opcode=8))
            opcode, masked, data = read_frame(sock)
            self.assertEqual((opcode, masked, len(data)), (1, True, 8388608))
            self.assertEqual(read_frame(sock), (8, True, close))
        with Peer(close_during_write) as peer:
            result = run_client(peer, '_duplex.ls')
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn('["close", 3504, true]', result.stdout)

    def test_invalid_accept(self):
        with Peer(lambda sock: handshake(sock, bad_accept=True)) as peer:
            result = run_client(peer, '_reject.ls')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.strip().endswith('405'), result.stdout)

    def test_unsupported_negotiation(self):
        for extra in (b'Sec-WebSocket-Extensions: permessage-deflate\r\n',
                      b'Sec-WebSocket-Extensions: ' + b'x' * 200 + b'\r\n',
                      b'Sec-WebSocket-Protocol: unexpected\r\n',
                      b'Sec-WebSocket-Accept: duplicate\r\n'):
            with self.subTest(extra=extra[:50]), Peer(lambda sock: handshake(sock, extra=extra)) as peer:
                result = run_client(peer, '_reject.ls')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.strip().endswith('405'), result.stdout)

    def test_invalid_frames(self):
        invalid = {
            'masked-server-frame': frame(b'wrong', masked=True),
            'invalid-utf8': frame(b'\xff'),
            'unexpected-continuation': frame(b'bad', opcode=0),
            'fragmented-control': frame(b'bad', opcode=9, final=False),
            'noncanonical-length': b'\x81\x7e\x00\x01x',
            'oversized-frame': b'\x81\x7f' + struct.pack('!Q', 1048577),
            'invalid-close': frame(b'\x03\xed', opcode=8),
            'invalid-close-utf8': frame(b'\x03\xe8\xff', opcode=8),
        }
        for label, payload in invalid.items():
            def send_bad(sock, payload=payload, label=label):
                handshake(sock)
                sock.sendall(payload)
                opcode, masked, status = read_frame(sock)
                expected = 1007 if 'utf8' in label else 1009 if label == 'oversized-frame' else 1002
                self.assertEqual((opcode, masked, status), (8, True, struct.pack('!H', expected)))
            with self.subTest(label=label), Peer(send_bad) as peer:
                result = run_client(peer, '_reject.ls')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(result.stdout.strip().endswith('405'), result.stdout)

    def test_timeout_preserves_partial_frame(self):
        def delayed(sock):
            handshake(sock)
            payload = frame(b'after timeout')
            sock.sendall(payload[:5])
            time.sleep(.1)
            sock.sendall(payload[5:])
            opcode, masked, close = read_frame(sock)
            self.assertEqual((opcode, masked), (8, True))
            sock.sendall(frame(close, opcode=8))
        with Peer(delayed) as peer:
            result = run_client(peer, '_timeout.ls')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('[406, "text", "after timeout"]', result.stdout)

    def tls_fixture(self):
        cert = ARTIFACTS / 'localhost.crt'
        key = ARTIFACTS / 'localhost.key'
        config = ARTIFACTS / 'localhost.cnf'
        config.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n[dn]\nCN=localhost\n[ext]\nsubjectAltName=IP:127.0.0.1\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,digitalSignature,keyEncipherment,keyCertSign\n')
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                        '-keyout', str(key), '-out', str(cert), '-config', str(config)],
                       check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        return cert, context

    def test_verified_wss(self):
        cert, context = self.tls_fixture()
        with Peer(exchange, context) as peer:
            result = run_client(peer, ca=cert)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertIn('"Welcome 世界"', result.stdout)

    def test_untrusted_wss_is_rejected(self):
        _, context = self.tls_fixture()
        with self.assertRaises(ssl.SSLError):
            with Peer(exchange, context) as peer:
                result = run_client(peer, '_reject.ls')
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                self.assertTrue(result.stdout.strip().endswith('405'), result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
