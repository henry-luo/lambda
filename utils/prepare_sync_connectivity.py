"""Build the unmodified, pinned native WebRTC mechanism into an optional prefix."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REVISION = "6b1e2e620f1e37f0eafeee702eaea0043cb305fd"  # libdatachannel v0.24.6


def run(*args):
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--openssl", default="/opt/homebrew/opt/openssl@3" if sys.platform == "darwin" else None)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("jobs must be 1..32")
    area = ROOT / "temp/sync-connectivity"
    source = area / "libdatachannel"
    build = area / "rtc-build"
    prefix = ROOT / "build/deps/connectivity"
    area.mkdir(parents=True, exist_ok=True)
    os.environ["TMPDIR"] = str(area)
    if not source.exists():
        run("git", "clone", "--branch", "v0.24.6", "--depth", "1", "--recurse-submodules",
            "--shallow-submodules", "https://github.com/paullouisageneau/libdatachannel.git", source)
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != REVISION:
        raise RuntimeError("dependency checkout differs from pinned revision")
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain",
                                     "--untracked-files=no"], text=True)
    if dirty:
        raise RuntimeError("dependency or submodules modified; refusing to build")
    options = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
               "-DBUILD_SHARED_LIBS=OFF", "-DNO_MEDIA=ON", "-DNO_WEBSOCKET=ON",
               "-DNO_EXAMPLES=ON", "-DNO_TESTS=ON", "-DOPENSSL_USE_STATIC_LIBS=TRUE"]
    if args.openssl:
        options.append("-DOPENSSL_ROOT_DIR=" + args.openssl)
    run("cmake", "-S", source, "-B", build, *options)
    run("cmake", "--build", build, "--parallel", args.jobs)
    run("cmake", "--install", build)
    submodules = subprocess.check_output(["git", "-C", str(source), "submodule", "status", "--recursive"], text=True)
    (prefix / "provenance.json").write_text(json.dumps({"revision": actual, "submodules": submodules,
                                                       "options": options}, indent=2) + "\n")


if __name__ == "__main__":
    main()
