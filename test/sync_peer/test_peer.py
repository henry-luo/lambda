"""Independent native processes exercise the public peer CLI over real sockets."""
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
TIER = os.environ.get("LAMBDA_EXEC_BACKEND", "auto")
AREA = ROOT / "temp" / "sync-peer" / ("process-" + TIER)
EXE = os.environ.get("LAMBDA_SYNC_EXE", str(ROOT / "lambda.exe"))
CLI = "lmd/package/sync/peer_cli.ls"
counter = 0
children = []
keys = {}


def launch(name, action, script=CLI, **fields):
    global counter
    counter += 1
    request = AREA / (str(counter) + "-" + action + ".json")
    request.write_text(json.dumps(dict(action=action, directory=str(AREA / name / "agent"), **fields)))
    env = dict(os.environ, LAMBDA_SYNC_REQUEST=str(request), LAMBDA_SYNC_KEY=keys[name],
               TMPDIR=str(AREA), LAMBDA_EXEC_BACKEND=TIER)
    out = (AREA / (str(counter) + "-" + action + ".out")).open("w")
    err = (AREA / (str(counter) + "-" + action + ".err")).open("w")
    process = subprocess.Popen([EXE, "run", "--no-log", script], cwd=ROOT, env=env, stdout=out, stderr=err)
    children.append(process)
    return process, out, err


def finish(job, success=True, timeout=180):
    process, out, err = job
    try:
        code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise AssertionError("native peer timed out: " + err.name)
    out.close()
    err.close()
    output = Path(out.name).read_text()
    errors = Path(err.name).read_text()
    accepted = code == 0 and "Error:" not in output and "Error:" not in errors and "error[E" not in errors
    assert accepted == success, (out.name, code, output[-1500:], errors[-2000:])
    return output


def run(name, action, success=True, **fields):
    return finish(launch(name, action, **fields), success)


def identifier(output):
    found = re.search(r"\bid:\s*\"?([0-9a-f]{64})", output)
    assert found, output
    return found[1]


def server(name):
    ready = AREA / ("ready-" + str(counter))
    job = launch(name, "serve", port=0, bind="0.0.0.0", max_sessions=1, ready=str(ready))
    deadline = time.monotonic() + 30
    while not ready.exists():
        if job[0].poll() is not None:
            finish(job)
            raise AssertionError("server exited before publishing port")
        assert time.monotonic() < deadline, "listener did not become ready"
        time.sleep(0.05)
    return job, int(ready.read_text())


def sync(a, b, peer, address="127.0.0.1"):
    job, port = server(b)
    output = run(a, "sync", url="ws://" + address + ":" + str(port) + "/sync", peer=peer)
    finish(job)
    assert "replicated:true" in output.replace(" ", ""), output
    return output


def check(condition, label):
    assert condition, label
    print("PASS " + label, flush=True)


def main():
    if AREA.exists():
        shutil.rmtree(AREA)
    AREA.mkdir(parents=True)
    ids = {}
    for name in ["a", "b", "stranger"]:
        (AREA / name / "agent").mkdir(parents=True, mode=0o700)
        (AREA / name / "tree").mkdir()
        keys[name] = secrets.token_hex(32)
        ids[name] = identifier(run(name, "device"))
        run(name, "export_identity", output=str(AREA / (name + ".device")))
    run("a", "init", root=str(AREA / "a/tree"), space="native-peer-space", label="Alice")
    run("a", "invite", recipient=str(AREA / "b.device"), output=str(AREA / "b.invitation"))
    run("b", "enroll", root=str(AREA / "b/tree"), label="Bob", invitation=str(AREA / "b.invitation"), expected=ids["a"])
    run("a", "invite", recipient=str(AREA / "stranger.device"), output=str(AREA / "stranger.invitation"))
    run("stranger", "enroll", root=str(AREA / "stranger/tree"), label="Unpaired with Bob",
        invitation=str(AREA / "stranger.invitation"), expected=ids["a"])
    run("stranger", "invite", recipient=str(AREA / "b.device"), output=str(AREA / "unused.invitation"))
    check(keys["a"] != keys["b"], "separate wrapping keys and encrypted enrollment")
    (AREA / "a/tree/folder").mkdir()
    (AREA / "a/tree/folder/note.txt").write_text("original")
    (AREA / "a/tree/data.bin").write_bytes(bytes(range(256)) * 20)
    (AREA / "a/tree/empty").write_bytes(b"")
    sync("a", "b", ids["b"])
    check((AREA / "b/tree/folder/note.txt").read_text() == "original", "initial network transfer")
    check((AREA / "b/tree/data.bin").read_bytes() == bytes(range(256)) * 20 and
          (AREA / "b/tree/empty").read_bytes() == b"", "binary and empty-file fidelity")
    (AREA / "a/tree/folder").rename(AREA / "a/tree/renamed")
    (AREA / "a/tree/renamed/note.txt").write_text("edited offline")
    (AREA / "b/tree/bob.txt").write_text("Bob offline")
    sync("a", "b", ids["b"])
    check((AREA / "b/tree/renamed/note.txt").read_text() == "edited offline" and
          not (AREA / "b/tree/folder").exists() and
          (AREA / "a/tree/bob.txt").read_text() == "Bob offline", "offline move/edit and bidirectional convergence")
    idle = sync("a", "b", ids["b"])
    check("downloaded:0" in idle.replace(" ", "") and "uploaded:0" in idle.replace(" ", ""), "idle exchange deduplicates objects")
    # A pinned wrong endpoint fails; the same listener remains usable afterwards.
    job, port = server("b")
    run("stranger", "sync", success=False, url="ws://127.0.0.1:" + str(port) + "/sync", peer=ids["b"])
    run("a", "sync", success=False, url="ws://127.0.0.1:" + str(port) + "/sync", peer=ids["stranger"])
    run("a", "sync", url="ws://127.0.0.1:" + str(port) + "/sync", peer=ids["b"])
    finish(job)
    check(True, "authorization failure does not stop listener")
    # Process death after listening leaves independently persisted repository state intact.
    job, port = server("b")
    job[0].terminate()
    job[0].wait(timeout=10)
    job[1].close()
    job[2].close()
    (AREA / "a/tree/restarted.txt").write_text("after restart")
    sync("a", "b", ids["b"])
    check((AREA / "b/tree/restarted.txt").read_text() == "after restart", "server restart and reconnect")
    job, port = server("b")
    (AREA / "a/tree/partial.txt").write_text("retained across transfer interruption")
    interrupted = AREA / "interrupted-ready"
    client = launch("a", "interrupt", script="test/sync_peer/_interrupt.ls",
                    url="ws://127.0.0.1:" + str(port) + "/sync", peer=ids["b"], ready=str(interrupted))
    deadline = time.monotonic() + 120
    while not interrupted.exists():
        if client[0].poll() is not None:
            finish(client)
            raise AssertionError("partial client exited before acknowledgment")
        assert time.monotonic() < deadline, "partial checkpoint not acknowledged"
        time.sleep(0.05)
    for stopped in [client, job]:
        stopped[0].kill()
        stopped[0].wait(timeout=10)
        stopped[1].close()
        stopped[2].close()
    sync("a", "b", ids["b"])
    check((AREA / "b/tree/partial.txt").read_text() == "retained across transfer interruption",
          "acknowledged object and partial checkpoint survive both process deaths")
    # Use a real host interface when available; wildcard binding is not confined to loopback.
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        address = probe.getsockname()[0]
    except OSError:
        address = None
    finally:
        probe.close()
    if address and not address.startswith("127."):
        sync("a", "b", ids["b"], address)
        check(True, "direct connection through non-loopback host interface")
    else:
        print("UNVERIFIED non-loopback interface unavailable", flush=True)
    (AREA / "a/tree/renamed/note.txt").write_text("Alice conflict")
    (AREA / "b/tree/renamed/note.txt").write_text("Bob conflict")
    sync("a", "b", ids["b"])
    status_a = run("a", "status")
    status_b = run("b", "status")
    check(re.search(r"kind:\s*'content'", status_a) and re.search(r"kind:\s*'content'", status_b),
          "concurrent file revisions retained for explicit resolution")
    if "--contention" in sys.argv:
        revoke = launch("a", "revoke", peer=ids["b"])
        invite = launch("a", "invite", recipient=str(AREA / "stranger.device"), output=str(AREA / "concurrent.invitation"))
        finish(revoke)
        finish(invite)
        membership = re.search(r"peers:\s*\[(.*?)\],", run("a", "status"), re.S)
        check(membership and ids["b"] not in membership[1] and ids["stranger"] in membership[1],
              "concurrent membership updates preserve revocation")
        run("a", "invite", recipient=str(AREA / "b.device"), output=str(AREA / "renewed.invitation"))
    else:
        print("UNQUALIFIED concurrent administration: LI-015; reproduce with --contention", flush=True)
    active, active_port = server("b")
    active_ready = AREA / "active-ready"
    resume = AREA / "active-resume"
    holding = launch("a", "hold", script="test/sync_peer/_interrupt.ls",
                     url="ws://127.0.0.1:" + str(active_port) + "/sync", peer=ids["b"],
                     ready=str(active_ready), resume=str(resume))
    deadline = time.monotonic() + 90
    while not active_ready.exists():
        if holding[0].poll() is not None:
            finish(holding)
            raise AssertionError("client did not establish held session")
        assert time.monotonic() < deadline, "held session did not become ready"
        time.sleep(0.05)
    run("b", "revoke", peer=ids["a"])
    resume.write_text("continue")
    check(finish(holding).strip() == "true", "revocation rejects next request in established session")
    active[0].terminate()
    active[0].wait(timeout=10)
    active[1].close()
    active[2].close()
    job, port = server("b")
    run("a", "sync", success=False, url="ws://127.0.0.1:" + str(port) + "/sync", peer=ids["b"])
    job[0].terminate()
    job[0].wait(timeout=10)
    job[1].close()
    job[2].close()
    check(True, "persisted revocation rejects subsequent sessions")


if __name__ == "__main__":
    try:
        main()
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
                child.wait()
