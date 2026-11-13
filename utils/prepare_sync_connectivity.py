"""Prepare pinned WebRTC sources and build static mbedTLS dependencies with Make."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

from split_paths import sibling_roots

ROOT = Path(__file__).resolve().parents[1]
REVISION = "6b1e2e620f1e37f0eafeee702eaea0043cb305fd"  # libdatachannel v0.24.6
SUBMODULES = ["deps/plog", "deps/usrsctp", "deps/libjuice"]


def mbedtls_libraries(system=None):
    """Use the host's selected TLS provider rather than finding a second copy."""
    system = system or ("macos" if sys.platform == "darwin" else
                        "linux" if sys.platform.startswith("linux") else "windows")
    config = json.loads((ROOT / "build_lambda_config.json").read_text())
    libraries = {entry["name"]: entry for entry in config["libraries"]}
    libraries.update({entry["name"]: entry for entry in config["platforms"][system].get("libraries", [])})
    input_root = str(sibling_roots()[0])
    return {name: {field: str((ROOT / entry[field].replace("${input_root}", input_root)).resolve())
                   for field in ("include", "lib")}
            for name in ("mbedtls", "mbedx509", "mbedcrypto") for entry in [libraries[name]]}


def configure_mbedtls():
    libraries = mbedtls_libraries()
    header = Path(libraries["mbedtls"]["include"]) / "mbedtls/build_info.h"
    if not header.is_file() or not re.search(r"^#define\s+MBEDTLS_VERSION_MAJOR\s+3\b", header.read_text(), re.M):
        raise RuntimeError("mbedTLS 3 headers missing; run the owning platform dependency setup first")
    if not all(Path(entry["lib"]).is_file() for entry in libraries.values()):
        raise RuntimeError("host mbedTLS static archives missing; run the owning platform dependency setup first")
    includes = dict.fromkeys(entry["include"] for entry in libraries.values())
    flags = " ".join(shlex.quote("-I" + path) for path in includes)
    os.environ["CPPFLAGS"] = (os.environ.get("CPPFLAGS", "") + " " + flags).strip()
    return libraries


def run(*args):
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    area = ROOT / "temp/sync-connectivity"
    source = area / "libdatachannel"
    prefix = ROOT / "build/deps/connectivity"
    command = [sys.executable, ROOT / "utils/build_dependencies.py", "libdatachannel",
               "--source", source, "--prefix", prefix, "--jobs", args.jobs]
    if args.dry_run:
        print("prepare pinned libdatachannel v0.24.6 and submodules: " + ", ".join(SUBMODULES))
        print(shlex.join(list(map(str, command))))
        return
    area.mkdir(parents=True, exist_ok=True)
    os.environ["TMPDIR"] = str(area)
    if not source.exists():
        run("git", "clone", "--branch", "v0.24.6", "--depth", "1",
            "https://github.com/paullouisageneau/libdatachannel.git", source)
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != REVISION:
        raise RuntimeError("dependency checkout differs from pinned revision")
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain",
                                     "--untracked-files=no"], text=True)
    if dirty:
        raise RuntimeError("dependency or submodules modified; refusing to build")
    status_command = ["git", "-C", str(source), "submodule", "status", "--recursive", "--", *SUBMODULES]
    submodules = subprocess.check_output(status_command, text=True)
    if any(line.startswith(("+", "U")) for line in submodules.splitlines()):
        raise RuntimeError("dependency submodule differs from pinned revision")
    if any(line.startswith("-") for line in submodules.splitlines()):
        run("git", "-C", source, "submodule", "update", "--init", "--recursive", "--depth", "1", "--", *SUBMODULES)
        submodules = subprocess.check_output(status_command, text=True)
    run(*command)
    # Remove the old CMake exports, which describe the previous OpenSSL linkage.
    for name in ("LibDataChannel", "LibJuice"):
        shutil.rmtree(prefix / "lib/cmake" / name, ignore_errors=True)
    provenance = {"revision": actual, "submodules": submodules, "build_system": "Premake/GNU Make",
                  "configuration": "release", "tls": "mbedtls", "plog_logging": False,
                  "media": False, "websocket": False, "mbedtls": mbedtls_libraries(),
                  "recipe_sha256": hashlib.sha256((ROOT / "build_deps_config.json").read_bytes()).hexdigest()}
    (prefix / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"sync-dependency: {error}", file=sys.stderr)
        sys.exit(1)
