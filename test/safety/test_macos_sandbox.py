#!/usr/bin/env python3
"""M0 raw-syscall probe for a signed macOS worker; not full M2 qualification."""

import argparse
import errno
import hashlib
import json
from pathlib import Path
import platform
import socket
import subprocess
import tempfile
from macos_fixture import ROOT, run, stage_bundle


def stage(directory: Path, sandboxed: bool, identity: str) -> Path:
    executable = directory / ("sandboxed-probe" if sandboxed else "control-probe")
    run(["xcrun", "clang", "-std=c17", "-Wall", "-Wextra", "-Werror",
         str(ROOT / "test/safety/macos_sandbox_probe.c"), "-o", str(executable)])
    return stage_bundle(directory, "Sandboxed" if sandboxed else "Control", executable, identity, sandboxed)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sign-identity", default="-", help="local code-signing identity; default is ad hoc")
    args = parser.parse_args()
    output = ROOT / "temp/app_safety_macos_probe"
    output.mkdir(parents=True, exist_ok=True)
    report = {"platform": platform.platform(), "sign_identity": args.sign_identity,
              "status": "unqualified", "m2_complete": False}
    result_path = output / "result.json"
    if platform.system() != "Darwin":
        report["reason"] = "macOS required"
        result_path.write_text(json.dumps(report, indent=2) + "\n")
        return 1
    try:
        # keep all artifacts for diagnosis; fixture sentinels contain no user data
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
        report["artifacts"] = str(directory.relative_to(ROOT))
        private = directory / "host-private"
        private.mkdir(mode=0o700)
        sentinel = private / "settings-sentinel"
        sentinel.write_bytes(b"host-owned test settings\n")
        sentinel.chmod(0o600)
        create_path = private / "new-file"
        control = stage(directory, False, args.sign_identity)
        worker = stage(directory, True, args.sign_identity)
        report["worker_sha256"] = hashlib.sha256(worker.read_bytes()).hexdigest()
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(4)
            port = str(listener.getsockname()[1])
            for label, executable in (("control", control), ("worker", worker)):
                report[label] = json.loads(run([str(executable), str(sentinel), str(create_path), port]))
        if any(value != 0 for value in report["control"].values()):
            raise RuntimeError("unsandboxed control failed; outer restrictions make the probe inconclusive")
        denied = {errno.EACCES, errno.EPERM}
        if any(value not in denied for value in report["worker"].values()):
            raise RuntimeError("worker did not deny every raw resource operation")
        if sentinel.read_bytes() != b"host-owned test settings\n" or create_path.exists():
            raise RuntimeError("worker changed host fixtures")
        report["status"] = "basic_denials_passed"
        report["remaining"] = ["per-principal container/IPC separation", "JIT profile",
                               "process/debug containment", "inherited handles", "broker integration"]
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
    result_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "basic_denials_passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
