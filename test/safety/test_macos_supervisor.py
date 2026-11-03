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

from macos_fixture import require_qualification, ROOT, run, stage_bundle, supervisor_arguments, supervisor_summary

IDENTIFIER = "org.lambda-script.safety-worker"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(directory: Path) -> None:
    with (directory / "build.log").open("w") as log:
        for command in (["make", "build-safety-worker"], ["make", "build-safety-host"],
                        ["make", "-C", "build/premake", "config=release_native",
                         "lambda-safety-host-fixture", "lambda-safety-supervisor-probe", "-j4"]):
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)


def exercise(directory: Path, label: str, binary: Path, *, sandbox=False, identifier=IDENTIFIER,
             entitlements=None, mutation=None, mode="real", limits=(30000, 10000, 536870912, 1048576),
             overwrite=False, consent=None, project_grant=True, runtime_image=None, hardened_runtime=True) -> dict:
    case = directory / label
    installation = case / "installation"
    installation.mkdir(parents=True)
    executable = stage_bundle(installation, "LambdaSafetyWorker", binary, "-", sandbox,
                              identifier=identifier, extra_entitlements=entitlements, runtime_image=runtime_image,
                              hardened_runtime=hardened_runtime)
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
    elif mutation == "runtime":
        runtime = executable.parents[1] / 'Frameworks/LambdaSafetyRuntime.dylib'
        with runtime.open('r+b') as stream:
            stream.seek(256); byte = stream.read(1); stream.seek(256)
            stream.write(bytes([byte[0] ^ 1]))
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
    if mode == "relative":
        entry.write_text('import helper: .helper\npn main() { let data = input("data.json")^; '
                         'output({answer: data.n + helper.offset}, "result.json", {format: \'json\'})^ }\n')
    if mode == "settings":
        target = home / ".lambda/setting/host-secret"
        entry.write_text(f"pn main() {{ input({json.dumps(str(target))})^ }}\n")
    if mode == "private":
        entry.write_text('pn main() { output({answer: 41}, "/.lambda/app-data/prefs.json", {format: \'json\'})^; '
                         'output({answer: 42}, "/.lambda/app-data/prefs.json", {format: \'json\'})^; '
                         'output({answer: 42}, "/.lambda/app-cache/preview.json", {format: \'json\'})^; '
                         'output("scratch", "/.lambda/app-temp/work.txt")^; '
                         'let cached = input("/.lambda/app-cache/preview.json")^; '
                         'let scratch = io.read("/.lambda/app-temp/work.txt")^; '
                         'let data = input("/.lambda/app-data/prefs.json")^; '
                         f'output({{answer: if (cached.answer == data.answer and scratch == "scratch") data.answer else -1}}, '
                         f'{json.dumps(str(report))}, {{format: \'json\'}})^ }}\n')
    original = b'{"old":true}\n'
    if overwrite:
        report.write_bytes(original)
    args = supervisor_arguments(home, installation, pin, entry, project if project_grant else None, limits, bool(consent))
    # Deliberately hand the host an unrelated open fd and ambient settings. Its
    # child must receive neither, even though the host itself legitimately does.
    sentinel = os.open(outside, os.O_RDONLY)
    env = dict(os.environ, SAFETY_SENTINEL="private", LAMBDA_HOME="/untrusted", DYLD_LIBRARY_PATH="/untrusted")
    start = time.monotonic()
    prompt = b""
    try:
        if consent in {"yes", "no", "change", "expired", "slow", "cleanup"}:
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
            if consent == "cleanup":
                temporary = list((home / ".lambda/app-data").glob("s-*/temp/r-*/files"))
                require_qualification(len(temporary) == 1)
                (temporary[0] / "alias").symlink_to(outside)
            if select.select([process.stdout], [], [], 0)[0]:
                process.kill(); process.communicate(timeout=5)
                raise RuntimeError(f"{label}: worker output exposed during host consent")
            if consent == "expired":
                # Start observation at the concrete prompt, not process spawn:
                # admission/bootstrap latency must not decide this consent test.
                time.sleep(limits[0] / 1000 + 0.2)
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
    summary = supervisor_summary(stderr)
    result = {"label": label, "exit": process.returncode, "elapsed": time.monotonic() - start,
              "host": summary, "output_size": len(stdout), "stderr": stderr.decode(errors="replace"),
              "prompt": prompt.decode(errors="replace"), "worker_sha256": pin}
    if mode == "probe" and stdout:
        result["probe"] = json.loads(stdout)
    if mode in {"real", "private", "relative"} and summary["success"]:
        if json.loads(report.read_text()) != {"answer": 42}:
            raise RuntimeError(f"{label}: incorrect authorized output")
        if mode == "private":
            private_files = list((home / ".lambda/app-data").glob("s-*/data/prefs.json"))
            require_qualification(len(private_files) == 1 and json.loads(private_files[0].read_text()) == {"answer": 42}, result)
    if overwrite and not summary["success"] and consent != "cleanup":
        expected = b"external-change\n" if consent == "change" else original
        if report.read_bytes() != expected:
            raise RuntimeError(f"{label}: unauthorized overwrite")
    temporary = list((home / ".lambda/app-data").glob("s-*/temp/r-*"))
    if consent == "cleanup":
        require_qualification(summary["cleanup_error"] == errno.EACCES and len(temporary) == 1, result)
        require_qualification(json.loads(report.read_text()) == {"answer": 42} and json.loads(outside.read_text()) == {"n": 99}, result)
    else:
        require_qualification(summary["cleanup_error"] == 0 and not temporary, result)
    (case / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(); parser.add_argument("--no-build", action="store_true")
    options = parser.parse_args()
    if platform.system() != "Darwin":
        raise SystemExit("macOS qualification only")
    output = ROOT / "temp/app_safety_macos_supervisor"
    output.mkdir(parents=True, exist_ok=True)
    cases = []
    def checkpoint():
        # failed builds/checks must not leave a previous passing report.
        (output / "result.json").write_text(json.dumps({"platform": platform.platform(),
            "status": "unqualified", "cases": cases, "M2_complete": False}, indent=2) + "\n")
    checkpoint()
    if not options.no_build:
        build(output)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    worker = ROOT / "lambda-safety-worker.exe"
    probe = ROOT / "lambda-safety-supervisor-probe.exe"
    def check(label, binary=worker, **kwargs):
        value = exercise(directory, label, binary, **kwargs); cases.append(value); checkpoint(); return value
    require_qualification(check("create")["host"]["success"])
    require_qualification(check("relative-paths", mode="relative")["host"]["success"])
    value = check("private-data", mode="private")
    require_qualification(value["host"]["success"] and value["host"]["challenges"] == 0, value)
    # Reuse the admitted project/image: only protected quota policy changes
    # between these real worker launches, with all persistent bytes retained.
    private_case = directory / "private-data"
    home, project, installation = (private_case / name for name in ("home", "project", "installation"))
    owners = list((home / ".lambda/app-data").glob("s-*"))
    require_qualification(len(owners) == 1)
    principal = owners[0].name[2:]
    preferences = owners[0] / "data/prefs.json"
    preserved = preferences.read_bytes()
    pin = value["worker_sha256"]
    for restore in (False, True):
        command = ["quota-defaults", "script", principal, "1"] if restore else ["quota", "script", principal, "0", "data", "1", "1024"]
        policy = json.loads(run([str(ROOT / "lambda-safety-host-fixture.exe"), "--fixture-storage", str(home), *command]))
        require_qualification(policy["quota_generation"] == (2 if restore else 1))
        if restore:
            (project / "result.json").unlink()
        process = subprocess.run(supervisor_arguments(home, installation, pin, project / "private.ls", project),
            cwd=ROOT, stdin=subprocess.DEVNULL, capture_output=True, timeout=40)
        summary = supervisor_summary(process.stderr)
        result = {"label": "private-quota-restored" if restore else "private-quota-denies", "exit": process.returncode,
            "host": summary, "quota": policy, "stderr": process.stderr.decode(errors="replace"), "worker_sha256": pin}
        cases.append(result); checkpoint()
        require_qualification(summary["verified"] and summary["launched"] and summary["served"] and not summary["cleanup_error"], result)
        require_qualification(summary["success"] == restore and (process.returncode == 0) == restore, result)
        require_qualification(preferences.read_bytes() == preserved and json.loads((project / "result.json").read_text()) == {"answer": 42})
    value = check("cleanup-alias", mode="private", overwrite=True, consent="cleanup")
    require_qualification(value["exit"] == 3 and not value["host"]["success"] and value["host"]["approvals"] == 1, value)
    require_qualification(check("overwrite-deny", overwrite=True)["host"]["challenges"] == 1)
    require_qualification(not check("outside", mode="outside")["host"]["success"])
    require_qualification(not check("loose-import", project_grant=False)["host"]["success"])
    require_qualification(not check("settings", mode="settings")["host"]["success"])
    for label, kwargs in [("incompatible-app-sandbox", {"sandbox": True}), ("wrong-id", {"identifier": "org.untrusted.worker"}),
                          ("network-entitlement", {"entitlements": {"com.apple.security.network.client": True}}),
                          ("jit-entitlement", {"entitlements": {"com.apple.security.cs.allow-jit": True}}),
                          ("wrong-pin", {"mutation": "pin"}), ("tampered-code", {"mutation": "tamper"}),
                          ("tampered-resource", {"mutation": "resource"})]:
        value = check(label, **kwargs)
        require_qualification(value["exit"] == 2 and not value["host"]["verified"] and not value["host"]["launched"], value)
    value = check("native-invariants", probe, mode="probe")
    native = value["probe"]
    require_qualification(native["sandboxed"], value)
    require_qualification(value["host"]["success"] and native["fresh_session"] and native["clean_env"] and not native["unexpected_fds"], value)
    require_qualification(native["tty_errno"] and native["read_errno"] == errno.EPERM and native["network_errno"] == errno.EPERM, value)
    require_qualification(native["fork_errno"] in {errno.EAGAIN, errno.EPERM}, value)
    native_case = directory / "native-invariants"
    image = native_case / "installation/LambdaSafetyWorker.app"
    entry = native_case / "project/drain.ls"; entry.write_text("null\n")
    drained = json.loads(run([str(probe), "monitor", str(image), sha(image / "Contents/MacOS/worker"),
                              str(entry), str(entry.parent)]))
    cases.append({"label": "exit-drain", "monitor": drained})
    checkpoint()
    require_qualification(drained["observed_exit"] and drained["success"] and drained["stop"] == 0, drained)
    require_qualification(drained["output"] == drained["stderr"] == 12288 and drained["exit"] == 0, drained)
    value = check("before-bootstrap", probe, mode="unconfined")
    require_qualification(value["host"]["success"] and value["host"]["requests"] == 1, value)
    for label, stop, limits in [("wall", 1, (250, 10000, 536870912, 1048576)),
                                ("cpu", 2, (3000, 200, 536870912, 1048576)),
                                ("memory", 3, (3000, 10000, 67108864, 1048576)),
                                ("output", 4, (3000, 10000, 536870912, 65536)),
                                ("disconnect", 1, (250, 10000, 536870912, 1048576))]:
        value = check("limit-" + label, probe, mode=label, limits=limits)
        require_qualification(value["exit"] == 4 and value["host"]["stop"] == stop and value["elapsed"] < 6, value)
    value = check("forged-channel", probe, mode="forged")
    require_qualification(not value["host"]["served"] and not value["host"]["success"], value)
    value = check("temporary-worker-crash", probe, mode="temp-crash")
    require_qualification(not value["host"]["success"] and value["host"]["signal"] == 5 and value["host"]["requests"] == 1, value)
    value = check("temporary-watchdog", probe, mode="temp-spin", limits=(2000, 10000, 536870912, 1048576))
    require_qualification(value["exit"] == 4 and value["host"]["stop"] == 1 and value["host"]["requests"] == 1, value)
    value = check("overwrite-approve", overwrite=True, consent="yes")
    require_qualification(value["host"]["success"] and value["host"]["approvals"] == 1, value)
    value = check("overwrite-reject", overwrite=True, consent="no")
    require_qualification(not value["host"]["success"] and value["host"]["approvals"] == 0, value)
    value = check("overwrite-race", overwrite=True, consent="change")
    require_qualification(not value["host"]["success"] and value["host"]["approvals"] == 0, value)
    value = check("headless-consent", overwrite=True, consent="headless")
    require_qualification(not value["host"]["success"] and value["host"]["approvals"] == 0, value)
    value = check("expired-consent", overwrite=True, consent="expired", limits=(10000, 10000, 536870912, 1048576))
    require_qualification(value["exit"] == 4 and value["host"]["stop"] == 1 and value["host"]["approvals"] == 0, value)
    value = check("slow-consent", overwrite=True, consent="slow", limits=(20000, 10000, 536870912, 1048576))
    require_qualification(value["host"]["success"] and value["host"]["approvals"] == 1, value)
    production = subprocess.run([str(ROOT / "lambda-safety-host.exe"), str(directory / "create/project/real.ls")],
                                cwd=ROOT, capture_output=True, timeout=10)
    require_qualification(production.returncode == 2 and b"publisher signature required" in production.stderr)
    production_symbols = run(["nm", "-g", str(ROOT / "lambda-safety-host.exe")])
    require_qualification("_security_grant_store_test_open" not in production_symbols)
    # Release LTO may inline the fixture store constructor entirely. The actual
    # isolated-home runs above are its positive control; test the production
    # command boundary as well, without forcing an otherwise-dead symbol export.
    fixture_option = subprocess.run([str(ROOT / "lambda-safety-host.exe"), "--fixture", str(directory)],
                                    cwd=ROOT, capture_output=True, timeout=10)
    require_qualification(fixture_option.returncode == 2 and b"Usage:" in fixture_option.stderr)
    run(["codesign", "--verify", "--strict", str(directory / "create/installation/LambdaSafetyWorker.app")])
    report = {"platform": platform.platform(), "status": "passed", "cases": cases, "passed": len(cases) + 1,
              "production_unsigned_refused": True, "M2_complete": False,
              "production_fixture_option_refused": True,
              "artifacts": {str(path.relative_to(ROOT)): sha(path) for path in
                            [worker, probe, ROOT / "lambda-safety-host.exe", ROOT / "lambda-safety-host-fixture.exe"]}}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "report": str(output / "result.json"), "M2_complete": False}))


if __name__ == "__main__":
    main()
