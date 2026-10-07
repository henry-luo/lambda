#!/usr/bin/env python3
"""Gate for the host export allowlist (doc/Lambda_Formal_Design.md D7.3.6).

The host executable exports only lambda/jube/jube_host_exports.txt. Checks:
  1. the host exports every listed symbol. The linker silently skips a listed
     symbol compiled hidden, so a missing LAMBDA_*_API marker on its owner
     declaration shows up here, not at link time;
  2. each built Jube module imports from the host only listed symbols, so a
     new host dependency is opened case by case instead of failing at load.

usage: utils/check_host_exports.py [--host EXE] [--modules] [MODULE ...]
"""
import argparse
import platform
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_premake import HOST_EXPORT_LIST, read_host_export_list  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DARWIN = platform.system() == "Darwin"


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def host_exports(exe: Path) -> set:
    if DARWIN:
        return {l.split()[-1][1:] for l in run(["nm", "-gU", str(exe)]).splitlines() if l.strip()}
    return {l.split()[-1].split("@")[0] for l in run(["nm", "-D", "--defined-only", str(exe)]).splitlines()
            if l.strip()}


def host_imports(module: Path) -> set:
    if DARWIN:
        # -undefined dynamic_lookup marks exactly the symbols bound against the host
        return {l.split()[2][1:] for l in run(["nm", "-m", "-u", str(module)]).splitlines()
                if "dynamically looked up" in l}
    # ELF: versioned imports bind to system libraries; the rest come from the host
    return {l.split()[-1] for l in run(["nm", "-D", "--undefined-only", str(module)]).splitlines()
            if l.strip() and "@" not in l and l.split()[0] == "U"}


def module_images() -> list:
    suffix = ".dylib" if DARWIN else ".so"
    return sorted((ROOT / "modules").glob(f"*/*{suffix}"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", type=Path, help="check this host executable's exports")
    parser.add_argument("--modules", action="store_true", help="check every built module in modules/")
    parser.add_argument("module", nargs="*", type=Path, help="check these module images")
    args = parser.parse_args()
    if platform.system() not in ("Darwin", "Linux"):
        print("HOST_EXPORTS: skipped (Windows hosts keep --export-all-symbols)")
        return 0
    listed = set(read_host_export_list())
    problems = []
    if args.host:
        missing = sorted(listed - host_exports(args.host))
        for name in missing:
            problems.append(f"{args.host.name} does not export listed {name}: "
                            "mark its owner declaration with LAMBDA_*_API")
    modules = list(args.module) + (module_images() if args.modules else [])
    for module in modules:
        for name in sorted(host_imports(module) - listed):
            problems.append(f"{module.name} imports unlisted host symbol {name}: open it "
                            f"case by case in {HOST_EXPORT_LIST.relative_to(ROOT)}")
    for problem in problems:
        print(f"HOST_EXPORTS: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"HOST_EXPORTS: passed ({len(listed)} listed, {len(modules)} modules checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
