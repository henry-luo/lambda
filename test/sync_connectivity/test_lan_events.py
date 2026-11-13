"""Independent LAN agents qualify event delivery, warm sessions and offline recovery."""
import importlib.util
import json
from pathlib import Path
import secrets
import shutil
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("connectivity_gate", ROOT / "test/sync_connectivity/test_connectivity.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
peer = gate.peer
AREA = ROOT / "temp/sync-lan-events" / ("process-" + peer.TIER)
peer.AREA = AREA
CLI = "lmd/package/sync/connectivity_cli.ls"
jobs = {}


def report(name):
    return gate.status(AREA / (name + ".status"))


def wait_for(predicate, label, names=("a", "b")):
    gate.wait_for(predicate, label, [jobs[n] for n in names if n in jobs], seconds=60)


def start(name):
    options = dict(mdns=True, rtc=False, interval_ms=100, retry_ms=1000,
                   verify_ms=300000,
                   status=str(AREA / (name + ".status")), ready=str(AREA / (name + ".ready")))
    jobs[name] = peer.launch(name, "run", script=CLI, options=options)


def stop(name):
    process, out, err = jobs.pop(name)
    process.terminate()
    process.wait(timeout=10)
    out.close()
    err.close()


def content(name, path, value):
    try:
        return (AREA / name / "tree" / path).read_text() == value
    except OSError:
        return False


def quiet(names=("a", "b")):
    wait_for(lambda: all(report(n).get("sessions", 0) > 0 and
                         report(n).get("active_connections", 1) == 0 and
                         report(n).get("phase") == "idle" for n in names), "idle peer disconnection", names)
    time.sleep(0.3)


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True, mode=0o700)
    ids = {}
    for name in ["a", "b", "c"]:
        (AREA / name / "agent").mkdir(parents=True, mode=0o700)
        (AREA / name / "tree").mkdir()
        peer.keys[name] = secrets.token_hex(32)
        ids[name] = peer.identifier(peer.run(name, "device"))
        peer.run(name, "export_identity", output=str(AREA / (name + ".device")))
    peer.run("a", "init", root=str(AREA / "a/tree"), space="events-" + secrets.token_hex(8), label="a")
    for name in ["b", "c"]:
        invitation = str(AREA / (name + ".invitation"))
        peer.run("a", "invite", recipient=str(AREA / (name + ".device")), output=invitation)
        peer.run(name, "enroll", root=str(AREA / name / "tree"), label=name,
                 invitation=invitation, expected=ids["a"])
    (AREA / "a/tree/initial.txt").write_text("before startup")
    start("a")
    start("b")
    wait_for(lambda: content("b", "initial.txt", "before startup"), "initial inventory exchange")
    quiet()
    peer.check(all(report(n).get("monitor") == "os" for n in ["a", "b"]),
               "startup inventory and first online exchange precede event-only monitoring")
    before = {n: report(n) for n in ["a", "b"]}
    time.sleep(2)
    peer.check(all(report(n)["scans"] == before[n]["scans"] and
                   report(n)["incremental_scans"] == before[n]["incremental_scans"] and
                   report(n)["connections"] == before[n]["connections"] for n in ["a", "b"]),
               "idle agents neither rescan the folder nor reconnect peers")
    (AREA / "a/tree/new/deep").mkdir(parents=True)
    (AREA / "a/tree/new/deep/one.txt").write_text("first batch")
    wait_for(lambda: content("b", "new/deep/one.txt", "first batch"), "watcher-created subtree")
    wait_for(lambda: all(report(n).get("active_connections", 0) > 0 for n in ["a", "b"]), "warm connection")
    warm = {n: report(n)["connections"] for n in ["a", "b"]}
    (AREA / "b/tree/reverse.txt").write_text("receiver edits")
    wait_for(lambda: content("a", "reverse.txt", "receiver edits"), "receiving-side change notification")
    for index in range(4):
        (AREA / "a/tree/new/deep/one.txt").write_text("batch " + str(index))
        time.sleep(0.03)
    wait_for(lambda: content("b", "new/deep/one.txt", "batch 3"), "batched edits")
    peer.check(all(report(n)["connections"] == warm[n] for n in ["a", "b"]) and
               report("a")["watch_events"] > before["a"]["watch_events"] and
               report("a")["incremental_scans"] > before["a"]["incremental_scans"],
               "OS events batch nested edits and both directions reuse an authenticated connection")
    quiet()
    peer.check(all(report(n)["active_connections"] == 0 for n in ["a", "b"]),
               "connections close after the configured quiet period")
    # Both peers initiate from idle, exercising simultaneous authentication and repository RPCs.
    (AREA / "a/tree/simultaneous-a.txt").write_text("A")
    (AREA / "b/tree/simultaneous-b.txt").write_text("B")
    wait_for(lambda: content("b", "simultaneous-a.txt", "A") and content("a", "simultaneous-b.txt", "B"),
             "simultaneous bidirectional changes")
    (AREA / "a/tree/new").rename(AREA / "a/tree/moved")
    wait_for(lambda: content("b", "moved/deep/one.txt", "batch 3") and not (AREA / "b/tree/new").exists(),
             "online directory move")
    replacement = AREA / "replacement.txt"
    replacement.write_text("atomic replacement")
    replacement.replace(AREA / "a/tree/moved/deep/one.txt")
    wait_for(lambda: content("b", "moved/deep/one.txt", "atomic replacement"), "atomic external save")
    (AREA / "a/tree/moved/deep/one.txt").unlink()
    wait_for(lambda: not (AREA / "b/tree/moved/deep/one.txt").exists(), "external deletion")
    peer.check(True, "simultaneous writers, directory moves, atomic saves and deletion converge")
    quiet()
    stop("b")
    (AREA / "a/tree/offline-a.txt").write_text("queued A")
    (AREA / "b/tree/offline-b.txt").write_text("external while stopped")
    start("b")
    wait_for(lambda: content("b", "offline-a.txt", "queued A") and content("a", "offline-b.txt", "external while stopped"),
             "restart catches offline changes")
    peer.check(True, "restart inventory and initial exchange reconcile external changes made while stopped")
    start("c")
    wait_for(lambda: content("c", "offline-b.txt", "external while stopped"), "new online peer catch-up", ("a", "b", "c"))
    (AREA / "b/tree/fanout.txt").write_text("B through A to C")
    wait_for(lambda: content("a", "fanout.txt", "B through A to C") and content("c", "fanout.txt", "B through A to C"),
             "authorized peer fan-out", ("a", "b", "c"))
    peer.check(True, "a newly online peer catches up and changed content reaches all authorized online peers")
    (AREA / "reports.json").write_text(json.dumps({n: report(n) for n in jobs}, indent=2) + "\n")
    print("LAN event acceptance complete", flush=True)


if __name__ == "__main__":
    try:
        main()
    finally:
        for name in list(jobs):
            stop(name)
        for child in peer.children:
            if child.poll() is None:
                child.kill()
            child.wait()
