#!/usr/bin/env python3
"""Real T0 Lambda + broker IPC in a signed macOS worker; partial M2 evidence."""

import argparse
import errno
import hashlib
import json
from pathlib import Path
import platform
import secrets
import selectors
import shutil
import socket
import subprocess
import tempfile

from macos_fixture import ROOT, run, stage_bundle


def build(output: Path) -> None:
    premake = shutil.which("premake5")
    if not premake:
        raise RuntimeError("premake5 is required")
    commands = [["make", "build-safety-worker"], ["make", "build-lambda-data"],
                ["make", "-C", "build/premake", "config=debug_native", "test_security_channel_gtest",
                 "test_security_admission_gtest", "-j4"],
                ["python3", "utils/generate_premake.py", "--variant", "safety-probe-worker",
                 "--output", "premake5.safety-probe.lua"],
                [premake, "gmake", "--file=premake5.safety-probe.lua"],
                ["make", "-C", "build_safety_probe_worker/premake", "config=release_native",
                 "lambda-safety-probe-worker", "-j4"]]
    with (output / "build.log").open("w") as log:
        for command in commands:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=900)


def exercise(directory: Path, executable: Path, label: str, port: int,
             sandboxed: bool, mode: str, native_probe: bool = True, admission_host: bool = False) -> dict:
    project = directory / label
    protected = project / "protected"
    protected.mkdir(parents=True, mode=0o700)
    secret = protected / "settings-sentinel"
    secret.write_text("host-private\n")
    installation = directory / (label + "-installation")
    if admission_host:
        project.chmod(0o700)
        (project / ".lambda").mkdir(mode=0o700)
        (project / ".lambda/setting").mkdir(mode=0o700)
        secret = project / ".lambda/setting/settings-sentinel"
        secret.write_text("host-private\n")
        installation.mkdir(mode=0o700)
    (project / "helper.ls").write_text("pub let offset = 1\n")
    data = project / "data.json"
    data.write_text('{"n":41}')
    outside = directory / (label + "-outside.json")
    outside.write_text('{"n":99}')
    report = project / "report.json"
    if mode == "settings":
        report = project / ".lambda/setting/forged.grant"
    previous = b'{"answer":"original"}\n'
    if mode in {"deny", "approve"}:
        report.write_bytes(previous)
    target = outside if mode in {"outside", "remember", "revoked", "changed-code"} else secret if mode == "protected" else data
    entry = project / "entry.ls"
    # The runtime obtains entry/helper/data bytes exclusively over the channel.
    entry.write_text("import helper: .helper\n"
                     f"pn main() {{ let data = input({json.dumps(str(target))})^; "
                     f"output({{answer: data.n + helper.offset}}, {json.dumps(str(report))}, {{format: 'json'}})^ }}\n")
    if mode == "path-task":
        text_file = project / "data.txt"
        text_file.write_text("payload")
        tree = project / "tree"
        (tree / "sub").mkdir(parents=True)
        (tree / "one").write_text("one")
        (tree / "sub/two").write_text("two")
        address = "(file./)" + "".join("[" + json.dumps(part) + "]" for part in project.parts[1:])
        entry.write_text(f"pn reader() {{ io.read({json.dumps(str(text_file))})^ }}\n"
                         f"pn main() {{ let h = start(reader); let text = wait(h)^; let base = {address}; "
                         f"output({{text: text, walk: len((base[\"tree\"]).**#), size: base[\"data.txt\"].size}}, "
                         f"{json.dumps(str(report))}, {{format: 'json'}})^ }}\n")
    if mode == "unsupported":
        source, side = json.dumps(str(data)), json.dumps(str(project / "side"))
        calls = [f"io.copy({source}, {side})", f"io.move({source}, {side})", f"io.delete({source})",
                 f"io.mkdir({side})", f"io.touch({source})", f"io.symlink({source}, {side})",
                 f"io.chmod({source}, 511)", f"io.rename({source}, {side})",
                 f"cmd(\"/usr/bin/touch\", [{side}])", f"io.grep({source}, \"n\")",
                 f"io.text_search({source}, \"n\")"]
        codes = ", ".join(call + " ^ { ^.code }" for call in calls)
        entry.write_text(f"pn main() {{ let codes = [{codes}]; "
                         f"output({{codes: codes}}, {json.dumps(str(report))}, {{format: 'json'}})^ }}\n")
    first, second = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    session = secrets.randbits(63) | 1
    host = worker = None
    raw_create = directory / (label + "-raw-create")
    try:
        admission_boot = None
        if admission_host:
            command = [str(directory / "host-admission.exe"), "--serve-admission-fixture",
                       str(first.fileno()), str(project), str(project), str(entry), str(installation), str(protected)]
            if mode in {"remember", "revoked", "changed-code"}:
                subprocess.run(command + ["remember", str(outside)], cwd=ROOT, check=True, capture_output=True,
                               pass_fds=(first.fileno(),), close_fds=True, text=True, timeout=20)
            if mode == "revoked":
                subprocess.run(command + ["revoke"], cwd=ROOT, check=True, capture_output=True,
                               pass_fds=(first.fileno(),), close_fds=True, text=True, timeout=20)
            if mode == "changed-code":
                (project / "helper.ls").write_text("pub let offset = 2\n")
            command += ["approve" if mode == "approve" else "deny"]
        else:
            command = [str(directory / "host-channel.exe"), "--serve-fixture",
                       str(first.fileno()), str(session), str(project), str(protected),
                       "approve" if mode == "approve" else "deny"]
        host = subprocess.Popen(command,
                                cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, pass_fds=(first.fileno(),), close_fds=True, text=True)
        if admission_host:
            with selectors.DefaultSelector() as ready:
                ready.register(host.stdout, selectors.EVENT_READ)
                if not ready.select(20):
                    raise RuntimeError(f"{label}: trusted admission did not complete")
                admission_boot = json.loads(host.stdout.readline())
            if mode == "changed-code":
                host_stdout, host_stderr = host.communicate(timeout=15)
                result = {"label": label, "mode": mode, "host_admission": True, "admission": admission_boot,
                          "host_exit": host.returncode, "worker_launched": False, "host_stderr": host_stderr}
                (directory / (label + "-result.json")).write_text(json.dumps(result, indent=2) + "\n")
                if host.returncode != 2 or admission_boot.get("admitted") or not admission_boot.get("identity_changed") or report.exists():
                    raise RuntimeError(f"{label}: changed source did not stop launch")
                return result
            if not admission_boot.get("admitted"):
                raise RuntimeError(f"{label}: trusted admission refused the fixture")
            session = admission_boot["session"]
        command = [str(executable), str(second.fileno()), str(session), str(entry)]
        if native_probe:
            command += [str(data), str(secret), str(raw_create), str(port), "confined" if sandboxed else "control"]
        worker = subprocess.Popen(command,
                                  cwd=project, env={"PATH": "/usr/bin:/bin", "LANG": "en_US.UTF-8"},
                                  stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  pass_fds=(second.fileno(),), close_fds=True, text=True)
        first.close(); second.close()
        worker_stdout, worker_stderr = worker.communicate(timeout=30)
        host_stdout, host_stderr = host.communicate(timeout=15)
        native = json.loads(worker_stdout.splitlines()[0]) if native_probe and worker_stdout else {}
        broker = json.loads(host_stdout) if host_stdout else {}
        result = {"label": label, "mode": mode, "sandboxed": sandboxed,
                  "host_admission": admission_host, "admission": admission_boot,
                  "worker_exit": worker.returncode, "host_exit": host.returncode, "native_probe": native_probe,
                  "native": native, "broker": broker,
                  "worker_stderr": worker_stderr, "host_stderr": host_stderr}
        (directory / (label + "-result.json")).write_text(json.dumps(result, indent=2) + "\n")
        expected_exit = 3 if mode in {"deny", "outside", "protected", "revoked", "settings"} else 0
        if worker.returncode != expected_exit or host.returncode or not broker.get("success"):
            raise RuntimeError(f"{label}: Lambda/broker execution failed; inspect {label}-result.json")
        if native_probe and native.get("unexpected_fds") != 0:
            raise RuntimeError(f"{label}: unexpected inherited descriptors")
        codes = [native.get(key) for key in ("read_errno", "write_errno", "create_errno", "connect_errno")]
        if native_probe and sandboxed:
            if any(code not in {errno.EPERM, errno.EACCES} for code in codes):
                raise RuntimeError(f"{label}: a native operation escaped containment")
        elif native_probe and codes != [0, 0, 0, 0]:
            raise RuntimeError("unsandboxed control failed; outer restrictions make this inconclusive")
        if mode in {"create", "approve", "path-task", "unsupported", "remember"}:
            expected = {"text": "payload", "walk": 4, "size": 7} if mode == "path-task" else {"answer": 42}
            if mode == "unsupported":
                expected = {"codes": [402] * 11}
                if data.read_text() != '{"n":41}' or (project / "side").exists():
                    raise RuntimeError(f"{label}: an unsupported adapter changed a file")
            if mode == "remember":
                expected = {"answer": 100}
                if admission_boot.get("restored") != 1:
                    raise RuntimeError(f"{label}: remembered file grant was not restored after host restart")
            if json.loads(report.read_text()) != expected:
                raise RuntimeError(f"{label}: brokered output is incorrect")
        elif mode == "deny":
            if report.read_bytes() != previous:
                raise RuntimeError("denied replacement changed the original")
        elif report.exists():
            raise RuntimeError(f"{label}: refused input still produced output")
        if broker.get("challenges") != (1 if mode in {"deny", "approve"} else 0):
            raise RuntimeError(f"{label}: incorrect trusted approval count")
        if secret.read_text() != "host-private\n" or raw_create.exists():
            raise RuntimeError(f"{label}: native fixture data changed")
        return result
    finally:
        first.close(); second.close()
        for process in (worker, host):
            if process and process.poll() is None:
                process.kill(); process.communicate(timeout=10)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sign-identity", default="-")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    output = ROOT / "temp/app_safety_macos_worker"
    output.mkdir(parents=True, exist_ok=True)
    report = {"platform": platform.platform(), "status": "unqualified", "m2_complete": False,
              "sign_identity": args.sign_identity, "cases": []}
    try:
        if platform.system() != "Darwin":
            raise RuntimeError("macOS required")
        if not args.no_build:
            build(output)
        binary = ROOT / "lambda-safety-probe-worker.exe"
        # Policy code can be hidden/local after archive linking. Inspect the
        # complete symbol table in both controls, including private storage.
        symbols = run(["nm", "-j", str(binary)])
        management = ("security_file_broker_", "security_grant_store_", "security_admission_", "security_policy_",
                      "security_private_storage_")
        if any(name in symbols for name in management):
            raise RuntimeError("worker unexpectedly links host grant/broker management")
        report["worker_management_symbols_absent"] = True
        report["worker_dependencies"] = run(["otool", "-L", str(binary)])
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
        # Pin the fixture hosts too; a concurrent build may relink shared test
        # executables between cases, invalidating this run's recorded hashes.
        for source, name in [("test_security_admission_gtest.exe", "host-admission.exe"),
                             ("test_security_channel_gtest.exe", "host-channel.exe")]:
            shutil.copy2(ROOT / "test" / source, directory / name)
        report["artifacts"] = str(directory.relative_to(ROOT))
        control = stage_bundle(directory, "LambdaControl", binary, args.sign_identity, False)
        worker = stage_bundle(directory, "LambdaSandboxed", binary, args.sign_identity, False)
        bootstrap = ROOT / "lambda-safety-worker.exe"
        bootstrap_symbols = run(["nm", "-j", str(bootstrap)])
        if any(name in bootstrap_symbols for name in management):
            raise RuntimeError("bootstrap worker unexpectedly links host management")
        bootstrap_worker = stage_bundle(directory, "LambdaT0Worker", bootstrap, args.sign_identity, False)
        report["signed_worker_sha256"] = hashlib.sha256(worker.read_bytes()).hexdigest()
        report["signed_bootstrap_sha256"] = hashlib.sha256(bootstrap_worker.read_bytes()).hexdigest()
        report["host_sha256"] = hashlib.sha256((directory / "host-channel.exe").read_bytes()).hexdigest()
        host_admission = directory / "host-admission.exe"
        host_symbols = run(["nm", "-j", str(host_admission)])
        if not all(name in host_symbols for name in management):
            raise RuntimeError("trusted admission fixture is missing a management component")
        report["host_admission_sha256"] = hashlib.sha256(host_admission.read_bytes()).hexdigest()
        report["entitlements"] = run(["codesign", "-d", "--entitlements", ":-", str(worker)])
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0)); listener.listen(16)
            port = listener.getsockname()[1]
            report["cases"].append(exercise(directory, control, "control", port, False, "create"))
            for mode in ("create", "deny", "approve", "outside", "protected", "path-task", "unsupported"):
                report["cases"].append(exercise(directory, worker, "worker-" + mode, port, True, mode))
            report["cases"].append(exercise(directory, bootstrap_worker, "bootstrap-worker", port,
                                            True, "create", native_probe=False))
            for mode in ("create", "deny", "approve", "protected", "settings", "remember", "revoked", "changed-code"):
                report["cases"].append(exercise(directory, worker, "admission-" + mode, port,
                                                True, mode, admission_host=True))
        report["status"] = "brokered_lambda_and_basic_denials_passed"
        report["remaining"] = ["production launcher and trusted approval UI",
                               "per-app container and process/debug qualification", "all unsupported adapter closure",
                               "watchdog/quotas/exhaustion", "persistent grants/restart/revocation qualification",
                               "JIT and other platform profiles"]
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "brokered_lambda_and_basic_denials_passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
