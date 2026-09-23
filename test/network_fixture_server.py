#!/usr/bin/env python3
"""Static fixture server with one delayed XHR response."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import sys
import time


class FixtureHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?", 1)[0] == "/":
            body = b"network fixture ready\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.split("?", 1)[0] == "/xhr_async_transport.html":
            body = b'''<!doctype html><html><body data-xhr-state="waiting"><script>
var xhr = new XMLHttpRequest();
xhr.open("GET", "xhr_async_transport_delay.json", true);
xhr.onload = function () {
  console.log("XHR_ASYNC_DONE=" + xhr.status + ":" + xhr.getResponseHeader("X-Async-Transport"));
  document.body.setAttribute("data-xhr-state", "done");
};
xhr.onerror = function () { console.log("XHR_ASYNC_ERROR"); };
xhr.send();
setTimeout(function () {
  console.log("XHR_ASYNC_TIMER=" + xhr.readyState);
  document.body.setAttribute("data-xhr-timer-state", String(xhr.readyState));
}, 10);
</script></body></html>'''
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.split("?", 1)[0] == "/xhr_async_transport_delay.json":
            time.sleep(0.4)
            body = b'{"transport":"libuv"}\n'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("X-Async-Transport", "libuv")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.split("?", 1)[0] == "/xhr_async_cancel.html":
            body = b'''<!doctype html><html><body><script>
var xhr = new XMLHttpRequest();
xhr.open("GET", "xhr_async_cancel_delay.json", true);
xhr.send();
setTimeout(function () {}, 60000);
for (;;) {}
</script></body></html>'''
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.split("?", 1)[0] == "/xhr_async_cancel_delay.json":
            time.sleep(15)
            body = b'{"cancelled":false}\n'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.split("?", 1)[0] == "/css_transition_teardown.html":
            body = b'''<!doctype html><html><head><style>
#box { width: 20px; height: 20px; transition: width 10s linear; }
#box.expanded { width: 40px; }
</style></head><body><div id="box"></div><script>
document.getElementById("box").className = "expanded";
</script></body></html>'''
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: network_fixture_server.py PORT DIRECTORY")
    handler = partial(FixtureHandler, directory=sys.argv[2])
    server = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), handler)
    server.serve_forever()


if __name__ == "__main__":
    main()
