#!/usr/bin/env python3
"""Native positive controls and direct attacks against the fixed worker policy."""
import errno
import hashlib
import json
from pathlib import Path
import platform
import shlex
import socket
import subprocess
import tempfile

from macos_fixture import require_qualification, ROOT, run, stage_bundle


def main() -> None:
    if platform.system() != "Darwin":
        raise SystemExit("macOS qualification only")
    output = ROOT / "temp/app_safety_process_sandbox"
    output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    report = {"platform": platform.platform(), "policy": "offline-broker-only-v1", "cases": [],
              "status": "unqualified", "artifacts": str(directory.relative_to(ROOT)), "M2_complete": False}
    def save_report():
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    save_report()  # a failed refresh must not leave a stale passing result
    binary = directory / "probe"
    uv_directory = Path(run(["pkg-config", "--variable=libdir", "libuv"]).strip())
    # Match the production worker's static dependency; hardened runtime rejects
    # unrelated dynamic-library signers before main, invalidating the control.
    uv_flags = [str(uv_directory / "libuv.a") if flag == "-luv" else flag for flag in
                shlex.split(run(["pkg-config", "--cflags", "--libs", "--static", "libuv"]))]
    run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
         "test/safety/process_sandbox_probe.c", "lib/process_sandbox.c", *uv_flags,
         "-framework", "IOKit", "-framework", "CoreFoundation", "-o", str(binary)])
    worker = stage_bundle(directory, "NativeOfflineWorker", binary, "-", False)
    report["binary_sha256"] = hashlib.sha256(worker.read_bytes()).hexdigest()
    own, other = directory / "own", directory / "other"
    own.write_bytes(b"app-A"); other.write_bytes(b"app-B")
    alias, hardlink = directory / "alias", directory / "hardlink"
    alias.symlink_to(other); hardlink.hardlink_to(other)
    cases = report["cases"]
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0)); listener.listen(4)
        for acquisition in ("fresh", "preheld"):
            for mode in ("control", "confined"):
                first, second = socket.socketpair()
                try:
                    created = directory / (mode + "-" + acquisition + "-new")
                    command = [str(worker), mode, acquisition, str(own), str(created), str(other),
                               str(alias), str(hardlink), str(listener.getsockname()[1]), str(second.fileno())]
                    result = subprocess.run(command, cwd=ROOT, env={"PATH": "/usr/bin:/bin"},
                                            capture_output=True, pass_fds=(second.fileno(),), timeout=10)
                    require_qualification(result.returncode == 0, (command, result.returncode, result.stderr))
                    value = json.loads(result.stdout)
                    case = {"mode": mode, "acquisition": acquisition, "native": value}; cases.append(case); save_report()
                    require_qualification(value["before"] == 0 and value["applied"] == (mode == "confined"), value)
                    require_qualification(value["system_errno"] == value["ipc_errno"] == 0, value)
                    require_qualification(value["kqueue_errno"] == value["uv_loop_error"] == 0, value)
                    require_qualification(first.recv(1) == b"Y")
                    # File capabilities cannot be revoked by installing this
                    # policy; the signed bootstrap separately rejects leaks.
                    held_expected = 0 if acquisition == "preheld" else -1
                    require_qualification(value["held_read_errno"] == value["held_map_errno"] == held_expected, value)
                    attacks = ("read", "write", "create", "other", "symlink", "hardlink", "connect", "bind", "fork", "process_info",
                               "public_model", "device_identifier", "signal", "trace", "trace_raw")
                    # The explicit get-properties operation must cover fresh
                    # lookups and pre-held rights; iokit* alone omitted it.
                    require_qualification((value["device_open_status"] != 0) == (mode == "confined"), value)
                    for attack in attacks:
                        expected = {errno.EPERM, errno.EACCES} if mode == "confined" else {0}
                        require_qualification(value[attack + "_errno"] in expected, (attack, value))
                    require_qualification((value["mach_errno"] != 0) == (mode == "confined"), value)
                    require_qualification(created.exists() == (mode == "control"), value)
                    exec_result = subprocess.run(command[:2] + ["exec"] + command[3:], cwd=ROOT,
                                                 capture_output=True, timeout=10)
                    require_qualification(exec_result.returncode == (77 if mode == "confined" else 0), exec_result)
                    case["exec_status"] = exec_result.returncode
                finally:
                    first.close(); second.close()
    require_qualification(own.read_bytes() == b"app-A" and other.read_bytes() == b"app-B")
    report["preheld_file_authority_survives_policy"] = True
    report["status"] = "passed"; save_report()
    print(json.dumps({"passed": len(cases), "report": str(output / "result.json")}))


if __name__ == "__main__":
    main()
