"""Measure directory opening through the first committed frame with a release host."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "temp/finder/performance/benchmark"
SOURCE = ROOT / "test/demo/finder"


def prepare(name, revision):
    target = WORK / name
    target.mkdir(parents=True, exist_ok=True)
    for path in SOURCE.iterdir():
        if path.suffix not in (".ls", ".js"):
            continue
        shutil.copyfile(path, target / path.name)
    shutil.copyfile(SOURCE / "finder.css", target / "finder.css")
    shutil.copytree(SOURCE / "files", target / "files", dirs_exist_ok=True)
    if revision:
        names = subprocess.check_output(
            ["git", "ls-tree", "--name-only", revision + ":test/demo/finder"], cwd=ROOT, text=True)
        for name in names.splitlines():
            if name.endswith((".ls", ".css")):
                content = subprocess.check_output(
                    ["git", "show", f"{revision}:test/demo/finder/{name}"], cwd=ROOT)
                (target / name).write_bytes(content)
    path = target / "finder.ls"
    source = path.read_text()
    # Instrument copies: the shipped app does not read clocks or publish profiling attributes.
    marker = "thumbnails:thumbs.schedule(owner,next.thumbnails)}"
    if marker not in source:
        raise ValueError("unsupported Finder revision: no retained thumbnail scheduler")
    source = source.replace("pn publish(owner,st) {", "pn publish(owner,st) {\n    let profile_started = clock()")
    source = source.replace("    return {*:next,", "    print(\"FINDER_PUBLISH \" ++ next.path ++ \" \" ++ string((clock()-profile_started)*1000))\n    return {*:next,", 1)
    source = source.replace(marker, marker[:-1] + ",profile:clock()}")
    source = source.replace("    var result = st\n    command", "    var result = st\n    let profile_started = clock()\n    command")
    source = source.replace("    let next = publish(owner,result)", "    print(\"FINDER_COMMAND \" ++ action ++ \" \" ++ string((clock()-profile_started)*1000))\n    let next = publish(owner,result)")
    source = source.replace("on finder_thumbnails(evt) {", """on finder_thumbnails(evt) {
    if (evt.event_phase == 2 and session.profile>0) {
        print("FINDER_FRAME " ++ session.path ++ " " ++ string((clock()-session.profile)*1000))
        session = {*:session,profile:0}
    }""")
    path.write_text(source)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./lambda.exe")
    parser.add_argument("--directory", default="./test")
    parser.add_argument("--reference", help="compare against Finder scripts at this Git revision")
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    binary, directory = Path(args.binary).resolve(), Path(args.directory).resolve()
    if b"VALIDATOR] validate_against_base_type" in binary.read_bytes():
        parser.error("use a release host before timing")
    if not directory.is_dir() or args.repetitions < 1:
        parser.error("provide a directory and at least one repetition")
    WORK.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SOURCE.parent / "mod_resources.ls", WORK / "mod_resources.ls")
    cases = [("current", prepare("current", None))]
    if args.reference:
        cases.insert(0, ("reference", prepare("reference", args.reference)))
    fixture = WORK / "events.json"
    fixture.write_text(json.dumps({"name": "Finder directory latency", "viewport": {"width": 1000, "height": 650}, "events": [
        {"type": "click", "target": {"selector": "#go"}},
        {"type": "type", "target": {"selector": "#folder-path"}, "text": str(directory), "clear": True},
        {"type": "click", "target": {"selector": "#go-confirm"}},
        {"type": "assert_script", "js": "document.querySelectorAll('#files .file').length>0"},
        {"type": "click", "target": {"selector": "#files .file"}},
        {"type": "assert_script", "js": "document.querySelectorAll('#files [aria-selected=true]').length===1"},
        {"type": "window_close"}, {"type": "assert_window_closed", "closed": True}
    ]}))
    results = []
    for iteration in range(args.repetitions):
        # Alternate ordering to reduce shared-host and filesystem warmup bias.
        for name, page in cases[::1 if iteration % 2 == 0 else -1]:
            tag = WORK / f"{name}-{iteration}"
            env = dict(os.environ, LAMBDA_LOG_LEVEL="ERROR", LAMBDA_LOG_FILE=str(tag) + "-native.log",
                       RADIANT_VIEW_TIMING_PATH=str(tag) + "-phases.tsv")
            with Path(str(tag) + ".log").open("w") as log:
                replay = subprocess.run([str(binary), "view", str(page), "--headless", "--event-file", str(fixture),
                                         "--event-result", str(tag) + ".json"], cwd=ROOT, env=env,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=180)
            outcome = json.loads(Path(str(tag) + ".json").read_text())
            if replay.returncode or outcome.get("result") != "PASS":
                raise SystemExit(f"Finder replay failed: {tag}.log")
            output = Path(str(tag) + ".log").read_text()
            def times(marker):
                return [float(value) for value in re.findall(marker + " " + re.escape(str(directory)) + r" ([0-9.e+-]+)", output)]
            publish, frame = times("FINDER_PUBLISH"), times("FINDER_FRAME")
            command = re.search(r"FINDER_COMMAND go-confirm ([0-9.e+-]+)", output)
            if len(publish) < 2 or len(frame) < 2 or not command:
                raise SystemExit(f"Finder timing markers missing: {tag}.log")
            record = {"version": name, "iteration": iteration,
                      "read_ms": float(command[1]), "publish_ms": publish[0], "settle_ms": frame[0],
                      "open_ms": float(command[1]) + publish[0] + frame[0],
                      "selection_ms": publish[1] + frame[1]}
            results.append(record)
            print(json.dumps(record), flush=True)
    summary = {name: {key: statistics.median(r[key] for r in results if r["version"] == name)
                      for key in ("read_ms", "publish_ms", "settle_ms", "open_ms", "selection_ms")}
               for name, _ in cases}
    (WORK / "results.json").write_text(json.dumps({"directory": str(directory), "binary": str(binary),
                                                "reference": args.reference, "repetitions": args.repetitions,
                                                "visible_entries": sum(not p.name.startswith(".") for p in directory.iterdir()),
                                                "runs": results, "median": summary}, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
