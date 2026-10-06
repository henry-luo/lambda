#!/usr/bin/env python3
"""Architecture gate for the rdb-drivers Jube module (vibe/Lambda_IO_RDB.md
section 13.11, RDB8).

Checks a built module image:
  1. it exports exactly the Jube entry point (`jube_module`);
  2. its dynamic dependencies are OS/system libraries only (RDB1);
  3. it imports no symbol by dynamic lookup, i.e. nothing from the host
     (D7.3.3; the module is linked without -undefined dynamic_lookup);
  4. Lambda's own driver objects call no socket or file API: vendor archives
     may, inside registered connections only (JA16.4);
  5. every `rdb:<driver>` the manifest provides resolves through the module
     (the loader cross-checks manifest against descriptor, D7.3.4), and an
     unknown driver does not (the probe can tell the difference).

usage: utils/check_rdb_module_architecture.py [--module-dir DIR] [--lambda EXE]
                                              [--objects DIR]
"""
import argparse
import json
import platform
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENTRY = "jube_module"

MACOS_DEP_PREFIXES = ("/usr/lib/", "/System/Library/Frameworks/")
LINUX_ALLOWED_NEEDED = re.compile(
    r"^(libc\.so\.\d+|libm\.so\.\d+|libpthread\.so\.\d+|libdl\.so\.\d+|librt\.so\.\d+|"
    r"ld-linux[\w.-]*\.so\.\d+)$")
# raw IO entry points Lambda's driver code must leave to the host bridge
BANNED_IMPORTS = {"socket", "connect", "bind", "listen", "accept", "getaddrinfo",
                  "open", "openat", "fopen", "creat"}


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def module_image(module_dir: Path) -> Path:
    for name in ("rdb-drivers.dylib", "rdb-drivers.so", "rdb-drivers.dll"):
        if (module_dir / name).exists():
            return module_dir / name
    raise SystemExit(f"rdb-arch: no module image in {module_dir}")


def check_exports(image: Path, problems: list):
    if platform.system() == "Darwin":
        names = [l.split()[-1].lstrip("_") for l in run(["nm", "-gU", str(image)]).splitlines() if l.strip()]
    else:
        names = [l.split()[-1] for l in run(["nm", "-D", "--defined-only", str(image)]).splitlines()
                 if l.strip() and l.split()[-2] in ("T", "D", "B", "R")]
    if sorted(names) != [ENTRY]:
        problems.append(f"exports must be exactly {ENTRY}, found {sorted(names)}")


def check_dependencies(image: Path, problems: list):
    if platform.system() == "Darwin":
        lines = run(["otool", "-L", str(image)]).splitlines()[1:]
        deps = [l.strip().split(" (")[0] for l in lines if l.strip()]
        for dep in deps:
            if dep.startswith("@rpath/rdb-drivers"):
                continue        # the image's own install name
            if not dep.startswith(MACOS_DEP_PREFIXES):
                problems.append(f"non-system dependency {dep}")
        bound = run(["nm", "-m", str(image)])
        lookups = [l for l in bound.splitlines() if "dynamically looked up" in l]
        for l in lookups:
            problems.append(f"dynamic-lookup import (host symbol?): {l.strip()}")
    else:
        for line in run(["readelf", "-d", str(image)]).splitlines():
            m = re.search(r"\(NEEDED\).*\[(.+)\]", line)
            if m and not LINUX_ALLOWED_NEEDED.match(m.group(1)):
                problems.append(f"non-system dependency {m.group(1)}")


def check_driver_objects(objects: Path, problems: list):
    files = sorted(objects.rglob("rdb_*.o")) if objects.exists() else []
    if not files:
        problems.append(f"no driver objects under {objects} to inspect")
        return
    for obj in files:
        for line in run(["nm", "-u", str(obj)]).splitlines():
            name = line.strip().split()[-1].lstrip("_") if line.strip() else ""
            if name in BANNED_IMPORTS:
                problems.append(f"{obj.name} calls {name}(): raw IO belongs to the host (JA16.4)")


def check_activation(module_dir: Path, lambda_exe: Path, problems: list, absent: bool = False):
    manifest = json.loads((module_dir / "module.json").read_text())
    drivers = [p.split(":", 1)[1] for p in manifest.get("provides", []) if p.startswith("rdb:")]
    if not drivers:
        problems.append("manifest provides no rdb:<driver>")
    probe = ('let r = input("rdb-arch-probe://127.0.0.1:1/x", \'{driver}\') ^ {{ "error" }}\n'
             'if (r == "error") "error" else "loaded"\n')
    with tempfile.TemporaryDirectory() as work:
        for driver in drivers + ["rdb-arch-unknown"]:
            # a resolved driver routes the unreachable target to the RDB layer,
            # whose failed open yields null; an unresolved one is an input()
            # error. Read from stdout, so it works on release hosts, which
            # compile logging out. The scratch cwd has no ./modules, so only
            # the module under test (JUBE_MODULE_PATH) can provide drivers.
            script = Path(work) / "probe.ls"
            script.write_text(probe.format(driver=driver))
            env = {"PATH": "/usr/bin:/bin", "JUBE_MODULE_PATH": str(module_dir.parent)}
            proc = subprocess.run([str(lambda_exe), str(script)], cwd=work, env=env,
                                  capture_output=True, text=True, timeout=60)
            lines = [l for l in proc.stdout.splitlines() if l.strip() and not re.match(r"^\d\d:\d\d:", l)]
            got = lines[-1].strip('"') if lines else "no output"
            want = "error" if absent or driver == "rdb-arch-unknown" else "loaded"
            if got != want:
                problems.append(f"driver '{driver}': expected {want}, got {got} "
                                f"(activation from {module_dir})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module-dir", default=str(ROOT / "modules" / "rdb-drivers"))
    parser.add_argument("--lambda", dest="lambda_exe", default=str(ROOT / "lambda.exe"))
    parser.add_argument("--objects", default=str(ROOT / "build" / "obj" / "rdb-drivers"))
    parser.add_argument("--expect-absent", action="store_true",
                        help="manifest-only descriptor: no image, and no driver may resolve")
    args = parser.parse_args()
    module_dir = Path(args.module_dir).resolve()
    if args.expect_absent:
        problems = []
        if any((module_dir / n).exists() for n in ("rdb-drivers.dylib", "rdb-drivers.so", "rdb-drivers.dll")):
            problems.append("a manifest-only bundle ships a module image")
        check_activation(module_dir, Path(args.lambda_exe).resolve(), problems, absent=True)
        for p in problems:
            print(f"FAIL {p}")
        print(f"rdb-arch: {module_dir} (descriptor only): {'FAILED' if problems else 'ok'}")
        return 1 if problems else 0
    image = module_image(module_dir)
    problems = []
    check_exports(image, problems)
    check_dependencies(image, problems)
    check_driver_objects(Path(args.objects), problems)
    check_activation(module_dir, Path(args.lambda_exe).resolve(), problems)
    for p in problems:
        print(f"FAIL {p}")
    print(f"rdb-arch: {image.relative_to(ROOT) if image.is_relative_to(ROOT) else image}: "
          f"{'FAILED' if problems else 'ok'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
