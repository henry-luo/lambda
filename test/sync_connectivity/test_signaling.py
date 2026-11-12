"""Actual S3 and fresh function invocations with a test-only answer-loss proxy."""
import http.server
import json
import os
from pathlib import Path
import secrets
import shutil
import sys
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "test/sync_cloud"))
import test_cloud as cloud
from test_connectivity import wait_for, status

peer = cloud.peer
AREA = ROOT / "temp/sync-connectivity" / ("signaling-" + os.environ.get("LAMBDA_CONNECTIVITY_RUN", peer.TIER))
peer.AREA = cloud.AREA = AREA
CLI = "lmd/package/sync/connectivity_cli.ls"
proxies = []


def proxy(target, names, capture, signals):
    state = {"block_answers": True, "requests": {}, "answers_hidden": 0, "injected": []}
    lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            raw = self.rfile.read(int(self.headers["Content-Length"]))
            request = cloud.decode(raw)["body"]
            kind = request["kind"]
            with lock:
                state["requests"][kind] = state["requests"].get(kind, 0) + 1
                inject = not state["block_answers"] and kind == "rendezvous_current" and request["body"]["kind"] == "offer"
                if inject:
                    name = names[request["actor"]]
                    filename = "direct-only-" + str(len(state["injected"]) + 1)
                    state["injected"].append(filename)
                    capture_index = len(state["injected"])
            if inject:
                # Admit a fresh external edit after this attempt's prior cloud round.
                (AREA / name / "tree" / filename).write_text(filename)
                (signals / ("capture-request-" + str(capture_index))).touch()
                wait_for((signals / ("capture-done-" + str(capture_index))).exists,
                         "warm external-edit capture", [capture], seconds=3)
            forwarded = urllib.request.Request(target, data=raw, headers={"content-type": "application/json"})
            try:
                with urllib.request.urlopen(forwarded, timeout=60) as response:
                    data = response.read()
            except Exception:
                self.send_error(502)
                return
            with lock:
                hide = state["block_answers"] and kind == "rendezvous_get" and request["body"]["kind"] == "answer"
                if hide:
                    state["answers_hidden"] += 1
            if hide:
                envelope = json.loads(data)
                for key, value in envelope[1][1]:
                    if cloud.decode(json.dumps([envelope[0], key]).encode()) == "body":
                        value[:] = ["array", []]
                data = json.dumps(envelope, separators=(",", ":")).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    proxies.append(server)
    server.evidence = state
    return server, state, lock


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True, mode=0o700)
    # Stage the selected executable through the deployment adapter's existing root interface.
    runtime = AREA / "runtime"
    runtime.mkdir()
    (runtime / "lambda.exe").symlink_to(Path(peer.EXE).resolve())
    (runtime / "lmd").symlink_to(ROOT / "lmd", target_is_directory=True)
    os.environ["LAMBDA_ROOT"] = str(runtime)
    ids = {}
    for name in ["a", "b"]:
        (AREA / name / "agent").mkdir(parents=True, mode=0o700)
        (AREA / name / "tree").mkdir()
        peer.keys[name] = secrets.token_hex(32)
        ids[name] = peer.identifier(peer.run(name, "device"))
        peer.run(name, "export_identity", output=str(AREA / (name + ".device")))
    peer.run("a", "init", root=str(AREA / "a/tree"), space="signaling-" + secrets.token_hex(8), label="a")
    peer.run("a", "invite", recipient=str(AREA / "b.device"), output=str(AREA / "invite"))
    peer.run("b", "enroll", root=str(AREA / "b/tree"), label="b", invitation=str(AREA / "invite"), expected=ids["a"])
    cloud.cloud("a", "bootstrap", members=[str(AREA / "b.device")], storage_protocol="catalog-2")
    host, port = cloud.function_server()
    signals = AREA / "capture"
    signals.mkdir(mode=0o700)
    initiator = min(ids, key=ids.get)
    capture = peer.launch(initiator, "capture_loop", script="test/sync_connectivity/_exchange.ls", signals=str(signals))
    wait_for((signals / "capture-ready").exists, "external-edit helper ready", [capture])
    relay, evidence, lock = proxy("http://127.0.0.1:" + str(port) + "/sync", {v: k for k, v in ids.items()}, capture, signals)
    config = dict(url="http://127.0.0.1:" + str(relay.server_port) + "/sync", authority=ids["a"])
    (AREA / "a/tree/before.txt").write_text("offline before startup")
    jobs = []
    started = time.monotonic()
    for name in ["a", "b"]:
        settings = dict(mdns=False, rtc=True, interval_ms=200, establish_ms=15000, retry_ms=5000,
                        cloud_ms=3600000, stop_after=1, rounds=500, status=str(AREA / (name + ".status")))
        jobs.append(peer.launch(name, "run", script=CLI, cloud=config, options=settings))
    wait_for(lambda: all(status(AREA / (n + ".status")).get("retries", 0) > 0 for n in ["a", "b"]),
             "lost answers must expire a bounded direct attempt", jobs, seconds=60)
    wait_for(lambda: all(status(AREA / (n + ".status")).get("transport") == "http" and
                         status(AREA / (n + ".status")).get("cloud_saved") and
                         status(AREA / (n + ".status")).get("phase") == "idle" for n in ["a", "b"]),
             "failed direct attempt must use HTTP fallback", jobs, seconds=30)
    peer.check(evidence["answers_hidden"] > 0 and all(status(AREA / (n + ".status")).get("sessions", 0) == 0
               for n in ["a", "b"]), "withheld answers trigger bounded failure and periodic HTTP fallback")
    fallback_seconds = time.monotonic() - started
    (AREA / "b/tree/returning.txt").write_text("offline work survives transport handover")
    with lock:
        evidence["block_answers"] = False
    unblocked = time.monotonic()
    for job in jobs:
        peer.finish(job, timeout=120)
    reports = [status(AREA / (n + ".status")) for n in ["a", "b"]]
    peer.check(all(r.get("sessions") == 1 and r.get("transport") == "peer" for r in reports),
               "finite signed signaling recovers an authenticated DataChannel after fallback")
    evidence.update(fallback_workflow_seconds=fallback_seconds, recovery_workflow_seconds=time.monotonic()-unblocked)
    peer.check((AREA / "a/tree/returning.txt").read_text() == "offline work survives transport handover" and
               (AREA / "b/tree/before.txt").read_text() == "offline before startup",
               "offline changes converge across HTTP/direct handover")
    moved = sum(r["progress"].get("uploaded", 0) + r["progress"].get("downloaded", 0) for r in reports)
    latest = evidence["injected"][-1]
    peer.check(moved > 0 and all((AREA / n / "tree" / latest).read_text() == latest for n in ["a", "b"])
               and not all(r["cloud_saved"] for r in reports),
               "direct delivery of new work does not inherit the prior cloud persistence flag")
    # A fresh process consumes a higher signaling sequence; abandoned session IDs are not resumed.
    fresh = cloud.cloud("a", "rendezvous_current", script="test/sync_retention/_control.ls", cloud=config,
                        body={"kind": "presence", "target": None})
    peer.run("a", "run", script=CLI, cloud=config, options=dict(mdns=False, rtc=False, rounds=1, interval_ms=100))
    restarted = cloud.cloud("a", "rendezvous_current", script="test/sync_retention/_control.ls", cloud=config,
                            body={"kind": "presence", "target": None})
    peer.check(restarted["record"]["body"]["sequence"] == fresh["record"]["body"]["sequence"] + 1 and
               restarted["record"]["body"]["session"] != fresh["record"]["body"]["session"],
               "a fresh native process advances its signaling fence and changes its instance ID")
    (AREA / "request-counts.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("Signaling/fallback acceptance complete", flush=True)


if __name__ == "__main__":
    try:
        main()
    finally:
        for server in proxies:
            server.shutdown()
            server.server_close()
            (AREA / "request-counts.json").write_text(json.dumps(server.evidence, indent=2) + "\n")
        for child in peer.children:
            if child.poll() is None:
                child.kill()
            child.wait()
