#!/usr/bin/env python3
"""Exercise the actual supervisor, including suspended-child admission and consent."""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import pty
import select
import shutil
import subprocess
import tempfile
import time

from macos_fixture import ROOT, run, stage_bundle

IDENTIFIER = "org.lambda-script.safety-worker"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(directory: Path) -> None:
    with (directory / "build.log").open("w") as log:
        for command in (["make", "build-safety-worker"], ["make", "build-safety-host"],
                        ["make", "-C", "build/premake", "config=release_native",
                         "lambda-safety-host-fixture", "lambda-safety-supervisor-probe", "-j4"]):
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)


def exercise(directory: Path, label: str, binary: Path, *, sandbox=True, identifier=IDENTIFIER,
             entitlements=None, mutation=None, mode="real", limits=(30000, 10000, 536870912, 1048576),
             overwrite=False, consent=None, project_grant=True) -> dict:
    case = directory / label
    installation = case / "installation"
    installation.mkdir(parents=True)
    executable = stage_bundle(installation, "LambdaSafetyWorker", binary, "-", sandbox,
                              identifier=identifier, extra_entitlements=entitlements)
    pin = sha(executable)
    if mutation == "pin":
        pin = "0" * 64
    elif mutation == "tamper":
        with executable.open("ab") as stream:
            stream.write(b"modified-after-signing")
    elif mutation == "resource":
        info = executable.parents[1] / "Info.plist"
        value = plistlib.loads(info.read_bytes()); value["CFBundleName"] = "tampered"
        info.write_bytes(plistlib.dumps(value))
    home, project = case / "home", case / "project"
    home.mkdir(mode=0o700); project.mkdir(mode=0o700)
    entry = project / (mode + ".ls")
    report = project / "result.json"
    outside = case / "private.json"; outside.write_text('{"n":99}')
    (project / "helper.ls").write_text("pub let offset = 1\n")
    (project / "data.json").write_text('{"n":41}')
    target = outside if mode == "outside" else project / "data.json"
    entry.write_text("import helper: .helper\n" +
                     f"pn main() {{ let data = input({json.dumps(str(target))})^; " +
                     f"output({{answer: data.n + helper.offset}}, {json.dumps(str(report))}, {{format: 'json'}})^ }}\n")
    if mode == "settings":
        target = home / ".lambda/setting/host-secret"
        entry.write_text(f"pn main() {{ input({json.dumps(str(target))})^ }}\n")
    original = b'{"old":true}\n'
    if overwrite:
        report.write_bytes(original)
    args = [str(ROOT / "lambda-safety-host-fixture.exe"), "--fixture", str(home), str(installation), pin,
            str(entry), str(project) if project_grant else "-", *map(str, limits), "ask" if consent else "deny"]
    # Deliberately hand the host an unrelated open fd and ambient settings. Its
    # child must receive neither, even though the host itself legitimately does.
    sentinel = os.open(outside, os.O_RDONLY)
    env = dict(os.environ, SAFETY_SENTINEL="private", LAMBDA_HOME="/untrusted", DYLD_LIBRARY_PATH="/untrusted")
    start = time.monotonic()
    prompt = b""
    try:
        if consent in {"yes", "no", "change", "expired", "slow"}:
            master, slave = pty.openpty()
            def terminal_session():
                os.setsid()
                import fcntl, termios
                fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
            process = subprocess.Popen(args, cwd=ROOT, env=env, stdin=slave, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, pass_fds=(sentinel, slave), preexec_fn=terminal_session)
            os.close(slave)
            deadline = time.monotonic() + 15
            while b"Type yes" not in prompt and time.monotonic() < deadline:
                if select.select([master], [], [], 0.2)[0]:
                    prompt += os.read(master, 65536)
                if process.poll() is not None:
                    break
            if b"Type yes" not in prompt:
                process.kill()
                stdout, stderr = process.communicate(timeout=5)
                raise RuntimeError(f"{label}: no trusted prompt: {stderr.decode(errors='replace')}")
            if consent == "change":
                report.write_bytes(b"external-change\n")
            if select.select([process.stdout], [], [], 0)[0]:
                process.kill(); process.communicate(timeout=5)
                raise RuntimeError(f"{label}: worker output exposed during host consent")
            if consent == "expired":
                time.sleep(2.2)
            if consent == "slow":
                time.sleep(11.2)
            os.write(master, b"yes\n" if consent != "no" else b"no\n")
            # Darwin drains the controlling tty during session teardown. Keep
            # consuming echo/prompt bytes while waiting for pipe output/exit.
            deadline = time.monotonic() + 35
            try:
                while True:
                    if select.select([master], [], [], 0)[0]:
                        try:
                            prompt += os.read(master, 65536)
                        except OSError as error:
                            if error.errno != errno.EIO:
                                raise
                    try:
                        stdout, stderr = process.communicate(timeout=0.1)
                        break
                    except subprocess.TimeoutExpired:
                        if time.monotonic() >= deadline:
                            process.kill()
                            raise
            finally:
                os.close(master)
        else:
            process = subprocess.Popen(args, cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, pass_fds=(sentinel,), start_new_session=True)
            stdout, stderr = process.communicate(timeout=40)
    finally:
        os.close(sentinel)
    lines = stderr.decode(errors="replace").splitlines()
    summary = json.loads(next(line for line in reversed(lines) if line.startswith('{"verified":')))
    result = {"label": label, "exit": process.returncode, "elapsed": time.monotonic() - start,
              "host": summary, "output_size": len(stdout), "stderr": stderr.decode(errors="replace"),
              "prompt": prompt.decode(errors="replace"), "worker_sha256": pin}
    if mode == "probe" and stdout:
        result["probe"] = json.loads(stdout)
    if mode == "real" and summary["success"]:
        if json.loads(report.read_text()) != {"answer": 42}:
            raise RuntimeError(f"{label}: incorrect authorized output")
    if overwrite and not summary["success"]:
        expected = b"external-change\n" if consent == "change" else original
        if report.read_bytes() != expected:
            raise RuntimeError(f"{label}: unauthorized overwrite")
    (case / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(); parser.add_argument("--no-build", action="store_true")
    options = parser.parse_args()
    if platform.system() != "Darwin":
        raise SystemExit("macOS qualification only")
    output = ROOT / "temp/app_safety_macos_supervisor"
    output.mkdir(parents=True, exist_ok=True)
    if not options.no_build:
        build(output)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    worker = ROOT / "lambda-safety-worker.exe"
    probe = ROOT / "lambda-safety-supervisor-probe.exe"
    cases = []
    def check(label, binary=worker, **kwargs):
        value = exercise(directory, label, binary, **kwargs); cases.append(value); return value
    assert check("create")["host"]["success"]
    assert check("overwrite-deny", overwrite=True)["host"]["challenges"] == 1
    assert not check("outside", mode="outside")["host"]["success"]
    assert not check("loose-import", project_grant=False)["host"]["success"]
    assert not check("settings", mode="settings")["host"]["success"]
    for label, kwargs in [("no-sandbox", {"sandbox": False}), ("wrong-id", {"identifier": "org.untrusted.worker"}),
                          ("network-entitlement", {"entitlements": {"com.apple.security.network.client": True}}),
                          ("jit-entitlement", {"entitlements": {"com.apple.security.cs.allow-jit": True}}),
                          ("wrong-pin", {"mutation": "pin"}), ("tampered-code", {"mutation": "tamper"}),
                          ("tampered-resource", {"mutation": "resource"})]:
        value = check(label, **kwargs)
        assert value["exit"] == 2 and not value["host"]["verified"] and not value["host"]["launched"], value
    value = check("native-invariants", probe, mode="probe")
    native = value["probe"]
    assert value["host"]["success"] and native["fresh_session"] and native["clean_env"] and not native["unexpected_fds"], value
    assert native["tty_errno"] and native["read_errno"] == errno.EPERM and native["network_errno"] == errno.EPERM, value
    assert native["fork_errno"] in {errno.EAGAIN, errno.EPERM}, value
    for label, stop, limits in [("wall", 1, (250, 10000, 536870912, 1048576)),
                                ("cpu", 2, (3000, 200, 536870912, 1048576)),
                                ("memory", 3, (3000, 10000, 67108864, 1048576)),
                                ("output", 4, (3000, 10000, 536870912, 65536)),
                                ("disconnect", 1, (250, 10000, 536870912, 1048576))]:
        value = check("limit-" + label, probe, mode=label, limits=limits)
        assert value["exit"] == 4 and value["host"]["stop"] == stop and value["elapsed"] < 6, value
    value = check("forged-channel", probe, mode="forged")
    assert not value["host"]["served"] and not value["host"]["success"], value
    value = check("overwrite-approve", overwrite=True, consent="yes")
    assert value["host"]["success"] and value["host"]["approvals"] == 1, value
    value = check("overwrite-reject", overwrite=True, consent="no")
    assert not value["host"]["success"] and value["host"]["approvals"] == 0, value
    value = check("overwrite-race", overwrite=True, consent="change")
    assert not value["host"]["success"] and value["host"]["approvals"] == 0, value
    value = check("headless-consent", overwrite=True, consent="headless")
    assert not value["host"]["success"] and value["host"]["approvals"] == 0, value
    value = check("expired-consent", overwrite=True, consent="expired", limits=(2000, 10000, 536870912, 1048576))
    assert value["exit"] == 4 and value["host"]["stop"] == 1 and value["host"]["approvals"] == 0, value
    value = check("slow-consent", overwrite=True, consent="slow", limits=(20000, 10000, 536870912, 1048576))
    assert value["host"]["success"] and value["host"]["approvals"] == 1, value
    production = subprocess.run([str(ROOT / "lambda-safety-host.exe"), str(directory / "create/project/real.ls")],
                                cwd=ROOT, capture_output=True, timeout=10)
    assert production.returncode == 2 and b"publisher signature required" in production.stderr
    production_symbols = run(["nm", "-g", str(ROOT / "lambda-safety-host.exe")])
    assert "_security_grant_store_test_open" not in production_symbols
    # Release LTO may inline the fixture store constructor entirely. The actual
    # isolated-home runs above are its positive control; test the production
    # command boundary as well, without forcing an otherwise-dead symbol export.
    fixture_option = subprocess.run([str(ROOT / "lambda-safety-host.exe"), "--fixture", str(directory)],
                                    cwd=ROOT, capture_output=True, timeout=10)
    assert fixture_option.returncode == 2 and b"Usage:" in fixture_option.stderr
    report = {"platform": platform.platform(), "cases": cases, "passed": len(cases) + 1,
              "production_unsigned_refused": True, "M2_complete": False,
              "production_fixture_option_refused": True,
              "artifacts": {str(path.relative_to(ROOT)): sha(path) for path in
                            [worker, probe, ROOT / "lambda-safety-host.exe", ROOT / "lambda-safety-host-fixture.exe"]}}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    run(["codesign", "--verify", "--strict", str(directory / "create/installation/LambdaSafetyWorker.app")])
    print(json.dumps({"passed": report["passed"], "report": str(output / "result.json"), "M2_complete": False}))


if __name__ == "__main__":
    main()
