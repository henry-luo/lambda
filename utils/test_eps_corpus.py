#!/usr/bin/env python3
"""Load every EPS through the document viewer, keeping logs under ./temp/."""

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--gui", action="store_true", help="open and automatically close native windows")
    args = parser.parse_args()
    executable = args.executable.resolve()
    fixtures = sorted((ROOT / "test/input/eps").rglob("*"))
    fixtures = [path for path in fixtures if path.suffix.lower() in (".eps", ".epsi")]
    if not fixtures:
        parser.error("no EPS fixtures found")
    output = ROOT / "temp" / ("eps-corpus-gui" if args.gui else "eps-corpus-headless")
    output.mkdir(parents=True, exist_ok=True)

    def check(path):
        relative = path.relative_to(ROOT / "test/input/eps")
        workspace = output / relative.with_suffix("")
        workspace.mkdir(parents=True, exist_ok=True)
        # Viewer and package lookup use the same resources as a checkout launch.
        for name in ("lmd", "test", "ref", "log.conf"):
            link = workspace / name
            if not link.exists() and (ROOT / name).exists():
                link.symlink_to(ROOT / name)
        env = dict(os.environ, LAMBDA_HOME=str(ROOT / "lmd"), LAMBDA_AUTO_CLOSE="1")
        command = [str(executable), "view"]
        if not args.gui:
            command.append("--headless")
        command.append(str(path))
        log = workspace / "viewer.log"
        try:
            with log.open("w") as stream:
                result = subprocess.run(command, cwd=workspace, env=env, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=args.timeout)
            status = result.returncode
        except subprocess.TimeoutExpired:
            status = "timeout"
        contents = log.read_text(errors="replace")
        errors = [line for line in contents.splitlines() if "[ERR!]" in line]
        passed = status == 0 and not errors
        return {"file": str(relative), "passed": passed, "exit": status,
                "errors": errors, "log": str(log.relative_to(ROOT))}

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(check, fixtures))
    report = {"executable": str(executable), "mode": "gui" if args.gui else "headless",
              "passed": sum(result["passed"] for result in results), "total": len(results),
              "results": results}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    for result in results:
        print(f"{'PASS' if result['passed'] else 'FAIL'} {result['file']}")
    print(f"{report['passed']}/{report['total']} loaded; {output.relative_to(ROOT)}/report.json")
    return 0 if report["passed"] == report["total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
