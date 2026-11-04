#!/usr/bin/env python3
"""Real host console controls in isolated profiles; no fetches or real settings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from macos_fixture import require_qualification

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser(); parser.add_argument("--no-build", action="store_true")
    options = parser.parse_args()
    output = ROOT / "temp/app_safety_update_controls"
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    report = {"status": "unqualified", "commands": rows, "passed": 0,
              "real_settings_opened": False, "delivery_available": False}
    def checkpoint():
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    # a failed refresh must replace stale passing evidence before any work.
    checkpoint()
    if not options.no_build:
        with (output / "build.log").open("w") as log:
            for command in (["make", "build-safety-host"],
                            ["make", "-C", "build/premake", "config=release_native", "lambda-safety-host-fixture", "-j4"],
                            ["make", "-C", "build/premake", "config=debug_native", "test_security_update_gtest", "-j4"]):
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=900, check=True)
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    home = directory / "home"; home.mkdir(mode=0o700)
    state = home / ".lambda/setting/package-updates.state"
    fixture = ROOT / "lambda-safety-host-fixture.exe"
    production = ROOT / "lambda-safety-host.exe"

    def invoke(*args, code=0):
        result = subprocess.run([str(fixture), "--fixture-updates", str(home), *args], cwd=ROOT,
                                env=dict(os.environ, HOME="/untrusted", LAMBDA_HOME="/untrusted"),
                                capture_output=True, text=True, timeout=20)
        rows.append({"command": list(args), "exit": result.returncode, "stdout": result.stdout, "stderr": result.stderr})
        checkpoint()
        require_qualification(result.returncode == code, rows[-1])
        return json.loads(result.stdout or result.stderr) if (result.stdout or result.stderr).lstrip().startswith("{") else None

    require_qualification(invoke("status", code=2)["result"] == "missing")
    require_qualification(not state.exists())
    require_qualification(invoke("init")["result"] == "ok")
    status = invoke("status"); require_qualification(status["mode"] == "enabled" and status["packages"] == [] and not status["delivery_available"])
    require_qualification(invoke("init", code=2)["result"] == "conflict")
    subprocess.run([str(ROOT / "test/test_security_update_gtest.exe"), "--seed-fixture", str(home)], cwd=ROOT, check=True, timeout=20)
    status = invoke("status"); require_qualification({p["package"] for p in status["packages"]} == {"lambda.fixture", "all"})
    invoke("pause", "lambda.fixture", "2000000000")
    status = invoke("status"); package = next(p for p in status["packages"] if p["package"] == "lambda.fixture")
    require_qualification(package["mode"] == "paused" and package["pause_until"] == 2000000000)
    invoke("disable", "all")
    status = invoke("status"); require_qualification(status["mode"] == "enabled" and next(p for p in status["packages"] if p["package"] == "all")["mode"] == "disabled")
    invoke("disable", "--all"); invoke("offline", "on"); invoke("restore", "--all")
    status = invoke("status"); require_qualification(status["offline"] and status["mode"] == "enabled")
    require_qualification(next(p for p in status["packages"] if p["package"] == "lambda.fixture")["mode"] == "paused")
    invoke("restore", "lambda.fixture")
    status = invoke("status"); package = next(p for p in status["packages"] if p["package"] == "lambda.fixture")
    require_qualification(package["mode"] == "enabled" and not package["pause_until"] and status["offline"])
    require_qualification(package["endpoint"] == "https://updates.example.org/repository/" and package["channel"] == "stable")
    invoke("offline", "off")
    before = state.read_bytes()
    for args in [("enable", "--all", "1"), ("pause", "--all", ""), ("pause", "--all", "-1"),
                 ("pause", "--all", "18446744073709551616"), ("offline", "maybe"),
                 ("admit", "https://untrusted.example.org/")]:
        invoke(*args, code=2)
        require_qualification(state.read_bytes() == before)
    require_qualification(invoke("disable", "not-admitted", code=2)["result"] == "missing")
    require_qualification(state.read_bytes() == before)
    # Each invocation is a separate host process; corrupt state is never silently
    # initialized or repaired by restore/init, and cannot expand app grants.
    state.write_bytes(before[:-1] + bytes([before[-1] ^ 1]))
    corrupt = state.read_bytes()
    require_qualification(invoke("restore", "--all", code=2)["result"] == "corrupt")
    require_qualification(invoke("init", code=2)["result"] == "conflict")
    require_qualification(state.read_bytes() == corrupt)
    for args in [("updates", "status"), ("updates", "init")]:
        result = subprocess.run([str(production), *args], cwd=ROOT, capture_output=True, timeout=10)
        require_qualification(result.returncode == 2 and b"publisher signature required" in result.stderr)
    result = subprocess.run([str(production), "--fixture-updates", str(home), "init"], cwd=ROOT, capture_output=True, timeout=10)
    require_qualification(result.returncode == 2 and b"Usage:" in result.stderr)
    report.update({"status": "passed", "passed": len(rows) + 3, "real_settings_opened": False,
              "delivery_available": False, "production_signature_gate": True,
              "artifacts": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in
                            ["lambda-safety-host.exe", "lambda-safety-host-fixture.exe", "test/test_security_update_gtest.exe"]}})
    (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    checkpoint()
    print(json.dumps({"passed": report["passed"], "report": str(output / "result.json"), "delivery_available": False}))


if __name__ == "__main__":
    main()
