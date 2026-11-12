"""Drop a committed publication's HTTP response once; no production fault switches."""
import http.server
import socket
import threading
import urllib.request


def start(target, decode, marker):
    class Proxy(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            raw=self.rfile.read(int(self.headers['Content-Length']))
            request=urllib.request.Request(target,data=raw,headers={'content-type':'application/json'})
            with urllib.request.urlopen(request,timeout=90) as response:
                body=response.read()
            if decode(raw)['body']['kind']=='commit' and not marker.exists() and decode(body).get('ok'):
                marker.write_text('publication committed; response deliberately dropped')
                self.close_connection=True
                self.connection.shutdown(socket.SHUT_RDWR)
                self.connection.close()
                return
            self.send_response(200)
            self.send_header('Content-Length',str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Proxy)
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    return server
