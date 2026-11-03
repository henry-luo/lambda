"""Shared signing fixture; all bundles and entitlement files stay under temp/."""

from pathlib import Path
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def require_qualification(condition, evidence=None):
    # Python -O removes assert statements, including any controls they invoke.
    if not condition:
        raise AssertionError(evidence)


def observe_bootstrap(command, directory, first, second, *, inherited,
                      stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE):
    """Own a bounded worker observation; terminate its valid IO control."""
    child = None
    try:
        child = subprocess.Popen(command, cwd=directory,
            env={'PATH': '/usr/bin:/bin', 'LANG': 'en_US.UTF-8'},
            stdin=stdin, stdout=stdout, stderr=stderr, pass_fds=inherited, close_fds=True)
        second.close()
        first.settimeout(5)
        request = first.recv(4096)
        if request:
            child.kill()
        output, error = child.communicate(timeout=5)
        return {'exit': child.returncode, 'broker_request_bytes': len(request),
                'stdout_bytes': len(output or b''), 'stderr': (error or b'').decode(errors='replace')}, output
    finally:
        first.close(); second.close()
        if child and child.poll() is None:
            child.kill(); child.communicate(timeout=5)


def supervisor_arguments(home: Path, installation: Path, pin: str, entry: Path,
                         project: Path | None, limits=(30000, 10000, 536870912, 1048576), ask=False) -> list[str]:
    return [str(ROOT / "lambda-safety-host-fixture.exe"), "--fixture", str(home), str(installation), pin,
            str(entry), str(project) if project else "-", *map(str, limits), "ask" if ask else "deny"]


def supervisor_summary(stderr: bytes) -> dict:
    import json
    lines = stderr.decode(errors="replace").splitlines()
    return json.loads(next(line for line in reversed(lines) if line.startswith('{"verified":')))


def run(command: list[str]) -> str:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(f"{command[0]} exited {result.returncode}: {result.stderr.strip()}")
    return result.stdout


def stage_bundle(directory: Path, name: str, binary: Path, identity: str, sandboxed: bool,
                 identifier: str | None = None, extra_entitlements: dict | None = None,
                 runtime_image: Path | None = None, hardened_runtime: bool = True) -> Path:
    app = directory / (name + ".app")
    contents = app / "Contents"
    executable = contents / "MacOS/worker"
    executable.parent.mkdir(parents=True)
    shutil.copy2(binary, executable)
    info = {"CFBundleIdentifier": identifier or "org.lambda-script.safety-qualification." + name.lower(),
            "CFBundleExecutable": "worker", "CFBundleName": "Lambda Safety Qualification",
            "CFBundlePackageType": "APPL", "CFBundleVersion": "1"}
    (contents / "Info.plist").write_bytes(plistlib.dumps(info))
    entitlements = directory / (name + ".entitlements.plist")
    entitlement_values = {"com.apple.security.app-sandbox": True} if sandboxed else {}
    entitlement_values.update(extra_entitlements or {})
    entitlements.write_bytes(plistlib.dumps(entitlement_values))
    flags = ["--options", "runtime"] if hardened_runtime else []
    if runtime_image:
        runtime = contents / 'Frameworks/LambdaSafetyRuntime.dylib'
        runtime.parent.mkdir()
        shutil.copy2(runtime_image, runtime)
        run(["codesign", "--force", "--sign", identity, *flags,
             "--entitlements", str(entitlements), str(runtime)])
    run(["codesign", "--force", "--sign", identity, *flags,
         "--entitlements", str(entitlements), str(app)])
    run(["codesign", "--verify", "--strict", str(app)])
    return executable
