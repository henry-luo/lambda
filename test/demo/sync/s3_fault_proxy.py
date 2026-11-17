"""One-shot TCP drop during a test object's upload; only used with a local S3 service."""
import select
import socket
import socketserver
import threading


class FaultProxy(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, target):
        self.target = target
        self.armed = threading.Event()
        self.dropped = threading.Event()
        super().__init__(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.serve_forever, daemon=True)
        self.thread.start()

    def stop(self):
        self.shutdown()
        self.server_close()
        self.thread.join()


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        client = self.request
        client.settimeout(20)
        upstream = socket.create_connection(self.server.target, timeout=20)
        try:
            data = b''
            while b'\r\n\r\n' not in data and len(data) < 65536:
                part = client.recv(8192)
                if not part: return
                data += part
            header, body = data.split(b'\r\n\r\n', 1)
            line = header.split(b'\r\n', 1)[0]
            length = next((int(x.split(b':', 1)[1].strip()) for x in header.split(b'\r\n')[1:]
                           if x.lower().startswith(b'content-length:')), 0)
            cut = self.server.armed.is_set() and line.startswith(b'PUT ') and b'/objects/' in line and length > 1024
            upstream.sendall(header + b'\r\n\r\n')
            if cut:
                while len(body) < min(512, length):
                    part = client.recv(512-len(body))
                    if not part: return
                    body += part
                upstream.sendall(body[:min(512, length-1)])
                self.server.armed.clear()
                self.server.dropped.set()
                return
            if body: upstream.sendall(body)
            while True:
                readable, _, _ = select.select([client, upstream], [], [], 30)
                if not readable: return
                for source in readable:
                    chunk = source.recv(65536)
                    if not chunk: return
                    (upstream if source is client else client).sendall(chunk)
        except (ConnectionError, OSError, ValueError):
            return
        finally:
            upstream.close()
