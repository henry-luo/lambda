#!/usr/bin/env python3
"""Static fixture server with one delayed XHR response."""

from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import time


class FixtureHandler(SimpleHTTPRequestHandler):
    def reply_fixture(self, body, content_type, status=200, headers=()):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        endpoint = self.path.split("?", 1)[0]
        if endpoint in ("/fetch_headers_network.html", "/fetch_async_snapshot.html",
                        "/fetch_async_timeout.html"):
            fixture = Path(__file__).parent / "browse/repro" / endpoint[1:]
            self.reply_fixture(fixture.read_bytes(), "text/html")
            return
        if endpoint == "/fetch_headers_redirect":
            self.reply_fixture(b"", "text/plain", 302, (
                ("Location", "/fetch_headers_echo"), ("X-Old-Response", "discarded")))
            return
        if endpoint == "/fetch_headers_echo":
            forbidden = any(self.headers.get(name) is not None for name in
                            ("Cookie", "Sec-Script", "X-HTTP-Method-Override"))
            self.reply_fixture(b"header fixture body", "text/plain", headers=(
                ("X-Request-Merge", self.headers.get("X-Merge", "missing")),
                ("X-Request-Latin", self.headers.get("X-Latin", "missing")),
                ("X-Request-Empty", "present" if "X-Empty" in self.headers else "missing"),
                ("X-Forbidden-Request", "present" if forbidden else "absent"),
                ("X-Repeat", "one"), ("X-Repeat", "two"),
                ("Set-Cookie", "fixture-cookie=hidden; Path=/")))
            return
        if self.path.split("?", 1)[0] == "/":
            body = b"network fixture ready\n"
            self.reply_fixture(body, "text/plain")
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
        if endpoint == "/xhr_async_transport_delay.json":
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
