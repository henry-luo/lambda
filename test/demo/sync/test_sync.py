"""Two native demo controllers pair and reconnect without manual repository setup."""
import importlib.util
from functools import partial
import os
from pathlib import Path
import secrets
import shutil

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("connectivity_gate", ROOT / "test/sync_connectivity/test_connectivity.py")
connectivity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(connectivity)
peer = connectivity.peer
read = connectivity.status
wait_for = partial(connectivity.wait_for, seconds=40)
AREA = ROOT / "temp/sync-demo" / ("process-" + peer.TIER)
peer.AREA = AREA
CLI = "test/demo/sync/tests/_device.ls"
USER = "demo-local-" + secrets.token_hex(6)


def launch(name, action, **fields):
    return peer.launch(name, action, script=CLI, profile=str(AREA / name / "profile"),
                       root=str(AREA / name / "tree"), events=str(AREA / (name + ".events")),
                       user=fields.pop("user", USER), **fields)


def completed(name):
    event = read(AREA / (name + ".events"))
    return event.get("type") == "status" and event.get("status", {}).get("sessions", 0) > 0


def stop(job):
    job[0].terminate()
    job[0].wait(timeout=10)
    job[1].close()
    job[2].close()


def paired_devices(name):
    peer.finish(launch(name, "status"))
    return read(AREA / (name + ".events"))["peers"]


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True, mode=0o700)
    for name in ["a", "b", "c"]:
        (AREA / name / "tree").mkdir(parents=True)
        peer.keys[name] = secrets.token_hex(32)
    host = launch("a", "host")
    wait_for(lambda: read(AREA / "a.events").get("type") == "pairing", "host pairing code", [host])
    code = read(AREA / "a.events")["code"]
    peer.finish(launch("b", "join", code=code, lifetime_ms=30000))
    peer.finish(host)
    a_id, b_id = paired_devices("b")[0], paired_devices("a")[0]
    peer.check(a_id != b_id, "same-user discovery and one-time code create independent mutually enrolled devices")
    profile_before = (AREA / "a/profile/profile.json").read_bytes()
    (AREA / "a/tree/from-a.txt").write_text("from A")
    (AREA / "b/tree/from-b.txt").write_text("from B")
    jobs = [launch(n, "run", options=dict(retry_ms=1000, interval_ms=200)) for n in ["a", "b"]]
    wait_for(lambda: (AREA / "a/tree/from-b.txt").exists() and (AREA / "b/tree/from-a.txt").exists(),
             "bidirectional automatic sync", jobs, seconds=60)
    wait_for(lambda: all(completed(n) for n in ["a", "b"]), "committed exchanges", jobs)
    peer.check((AREA / "a/tree/from-b.txt").read_text() == "from B" and
               (AREA / "b/tree/from-a.txt").read_text() == "from A", "external file changes sync automatically in both directions")
    for job in jobs:
        stop(job)
    (AREA / "a/tree/from-a.txt").rename(AREA / "a/tree/moved.txt")
    (AREA / "b/tree/offline.txt").write_text("offline B")
    jobs = [launch(n, "run", options=dict(retry_ms=1000, interval_ms=200)) for n in ["a", "b"]]
    wait_for(lambda: (AREA / "b/tree/moved.txt").exists() and (AREA / "a/tree/offline.txt").exists(),
             "restart and offline convergence", jobs, seconds=60)
    wait_for(lambda: all(completed(n) for n in ["a", "b"]), "committed offline exchanges", jobs)
    peer.check(not (AREA / "b/tree/from-a.txt").exists() and
               (AREA / "a/profile/profile.json").read_bytes() == profile_before,
               "restart reuses the profile and merges offline moves and new files without re-pairing")
    for job in jobs:
        stop(job)
    # A new enrollment window cannot accept the spent code.
    (AREA / "a.events").unlink()
    host = launch("a", "host")
    wait_for(lambda: read(AREA / "a.events").get("type") == "pairing", "replacement code", [host])
    fresh = read(AREA / "a.events")["code"]
    peer.check(fresh != code, "another pairing window creates a fresh code")
    peer.finish(launch("c", "join", code=fresh, user=USER+"-other", lifetime_ms=5000), success=False)
    peer.check(paired_devices("a") == [b_id] and not (AREA / "c/profile/profile.json").exists(),
               "a different user ID cannot find or enroll through this account's code")
    peer.finish(launch("c", "join", code=code, lifetime_ms=5000), success=False)
    retries = read(AREA / "a.events").get("attempts", 0)
    peer.check(retries >= 2 and paired_devices("a") == [b_id] and not (AREA / "c/profile/profile.json").exists(),
               "candidate retries preserve one-time-code rejection without enrolling a device")
    peer.finish(launch("c", "join", code=fresh, lifetime_ms=30000))
    peer.finish(host)
    peer.check(len(paired_devices("a")) == 2, "a fresh code adds another device without replacing existing grants")
    print("Sync demo local acceptance complete", flush=True)


if __name__ == "__main__":
    try:
        main()
    finally:
        for child in peer.children:
            if child.poll() is None:
                child.kill()
            child.wait()
