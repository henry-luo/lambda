"""Independent devices exercise native discovery and authenticated DataChannel transfer."""
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import secrets
import shutil
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("peer_gate", ROOT / "test/sync_peer/test_peer.py")
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)
AREA = ROOT / "temp/sync-connectivity" / ("process-" + os.environ.get("LAMBDA_CONNECTIVITY_RUN", peer.TIER))
peer.AREA = AREA
CLI = "lmd/package/sync/connectivity_cli.ls"


def wait_for(predicate, label, jobs=(), seconds=60):
    deadline = time.monotonic() + seconds
    while not predicate():
        for job in jobs:
            assert job[0].poll() is None, "process ended before " + label + ": " + job[2].name
        assert time.monotonic() < deadline, label
        time.sleep(0.05)


def status(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def exchange_pair(ids, suffix, marker=None):
    signals = AREA / suffix
    signals.mkdir(mode=0o700)
    server = peer.launch("b", "exchange", script="test/sync_connectivity/_exchange.ls",
                         role="server", signals=str(signals), peer=ids["a"])
    client = peer.launch("a", "exchange", script="test/sync_connectivity/_exchange.ls",
                         role="client", signals=str(signals), peer=ids["b"], marker=marker)
    return server, client


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True, mode=0o700)
    ids = {}
    for name in ["a", "b"]:
        (AREA / name / "agent").mkdir(parents=True, mode=0o700)
        (AREA / name / "tree").mkdir()
        peer.keys[name] = secrets.token_hex(32)
        ids[name] = peer.identifier(peer.run(name, "device"))
        peer.run(name, "export_identity", output=str(AREA / (name + ".device")))
    peer.run("a", "init", root=str(AREA / "a/tree"), space="connectivity-" + secrets.token_hex(8), label="a")
    peer.run("a", "invite", recipient=str(AREA / "b.device"), output=str(AREA / "invitation"))
    peer.run("b", "enroll", root=str(AREA / "b/tree"), label="b", invitation=str(AREA / "invitation"), expected=ids["a"])
    (AREA / "a/tree/lan.txt").write_text("automatic discovery without cloud")
    jobs = []
    started = time.monotonic()
    for name in ["a", "b"]:
        options = dict(mdns=True, rtc=False, interval_ms=200, retry_ms=1000, rounds=200,
                       stop_after=1, status=str(AREA / (name + ".status")), ready=str(AREA / (name + ".ready")))
        jobs.append(peer.launch(name, "run", script=CLI, options=options))
    for job in jobs:
        peer.finish(job, timeout=60)
    reports = [status(AREA / (n + ".status")) for n in ["a", "b"]]
    peer.check(all(r.get("sessions") == 1 and r.get("transport") == "peer" for r in reports)
               and (AREA / "b/tree/lan.txt").read_text() == "automatic discovery without cloud",
               "private Bonjour discovery authenticates LAN peers without a cloud endpoint")
    discovery_seconds = time.monotonic() - started
    # Keep discovery/capture and the repository DataChannel gate independent.
    large = bytes(range(251)) * 70000  # over 16 MiB; no whole-payload protocol envelope
    (AREA / "a/tree/large.bin").write_bytes(large)
    (AREA / "b/tree/offline.txt").write_text("offline work")
    started = time.monotonic()
    server, client = exchange_pair(ids, "signals")
    transferred = peer.finish(client)
    peer.finish(server)
    peer.check(hashlib.sha256((AREA / "b/tree/large.bin").read_bytes()).digest() == hashlib.sha256(large).digest()
               and (AREA / "a/tree/offline.txt").read_text() == "offline work",
               "real DataChannel installs a large binary and merges independent offline changes")
    counters = {key: int(re.search(r"\b" + key + r":\s*(\d+)", transferred)[1])
                for key in ["sent_bytes", "received_bytes"]}
    (AREA / "measurements.json").write_text(json.dumps(dict(discovery_seconds=discovery_seconds,
        channel_workflow_seconds=time.monotonic()-started, verified_file_bytes=len(large),
        **counters), indent=2) + "\n")
    (AREA / "b/tree/interrupted.txt").write_text("survives interrupted DataChannel transfer")
    marker = AREA / "partial-object"
    server, client = exchange_pair(ids, "signals-interrupt", str(marker))
    wait_for(marker.exists, "partial transfer must commit an authenticated object", [server, client])
    for job in [client, server]:
        job[0].kill()
        job[0].wait()
        job[1].close()
        job[2].close()
    server, client = exchange_pair(ids, "signals-resume")
    peer.finish(client)
    peer.finish(server)
    peer.check((AREA / "a/tree/interrupted.txt").read_text() == "survives interrupted DataChannel transfer",
               "both agents restart and finish a transfer after a partially committed object")
    repeats = int(os.environ.get("LAMBDA_CONNECTIVITY_REPEATS", "0"))
    assert 0 <= repeats <= 100
    for index in range(repeats):
        server, client = exchange_pair(ids, "reconnect-" + str(index))
        peer.finish(client)
        peer.finish(server)
        peer.check(True, "independent native reconnect " + str(index + 1))
    print("Connectivity local acceptance complete", flush=True)


if __name__ == "__main__":
    try:
        main()
    finally:
        for child in peer.children:
            if child.poll() is None:
                child.kill()
            child.wait()
