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


def prepare(name, revision=None, source_dir=SOURCE, work=WORK):
    target = work / name
    target.mkdir(parents=True, exist_ok=True)
    for path in source_dir.iterdir():
        if path.suffix not in (".ls", ".js"):
            continue
        shutil.copyfile(path, target / path.name)
    shutil.copyfile(source_dir / "finder.css", target / "finder.css")
    shutil.copytree(source_dir / "files", target / "files", dirs_exist_ok=True)
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
    if "thumbnails:thumbs.schedule(owner,next.thumbnails)" not in source:
        raise ValueError("unsupported Finder revision: no retained thumbnail scheduler")
    # Anchor markers to their own procedures: other handlers also return a next session.
    start, end = source.index("pn publish(owner,st) {"), source.index("\nfn navigate(")
    publish = source[start:end].replace("pn publish(owner,st) {", "pn publish(owner,st) {\n    let profile_started = clock()")
    publish = publish.replace("    return {*:next,", "    print(\"FINDER_PUBLISH \" ++ next.path ++ \" \" ++ string((clock()-profile_started)*1000) ++ \" \" ++ string(len(next.selected)) ++ \"\\n\")\n    return {*:next,profile:clock(),", 1)
    source = source[:start] + publish + source[end:]
    source, dispatch_count = re.subn(r"(pn dispatch\(owner,st,action(?:,destination=null)?\) \{)",
                                   r"\1\n    let profile_started = clock()", source, count=1)
    if dispatch_count != 1:
        raise ValueError("unsupported Finder revision: no command dispatcher")
    source = source.replace("    let next = publish(owner,result)", "    print(\"FINDER_COMMAND \" ++ action ++ \" \" ++ string((clock()-profile_started)*1000) ++ \"\\n\")\n    let next = publish(owner,result)")
    source = source.replace("on finder_thumbnails(evt) {", """on finder_thumbnails(evt) {
    if (evt.event_phase == 2 and session.profile>0) {
        print("FINDER_FRAME " ++ session.path ++ " " ++ string((clock()-session.profile)*1000) ++ " " ++ string(len(session.selected)) ++ "\\n")
        session = {*:session,profile:0}
    }""")
    path.write_text(source)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./lambda.exe")
    parser.add_argument("--directory", default="./test")
    parser.add_argument("--reference", help="compare against Finder scripts at this Git revision")
    parser.add_argument("--reference-dir", type=Path, help="compare against a saved Finder source directory")
    parser.add_argument("--reference-binary", type=Path, help="use a different release host for the reference")
    parser.add_argument("--view", choices=("icons", "list", "columns"), default="icons")
    parser.add_argument("--output-dir", type=Path, default=WORK, help="keep this comparison's scripts, logs and results together")
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    if args.reference and args.reference_dir:
        parser.error("choose a revision or a saved source directory")
    if args.reference_binary and not (args.reference or args.reference_dir):
        parser.error("a reference host also needs reference Finder sources")
    binary, directory = Path(args.binary).resolve(), Path(args.directory).resolve()
    if b"VALIDATOR] validate_against_base_type" in binary.read_bytes():
        parser.error("use a release host before timing")
    reference_binary = args.reference_binary.resolve() if args.reference_binary else binary
    if b"VALIDATOR] validate_against_base_type" in reference_binary.read_bytes():
        parser.error("use a release reference host before timing")
    if not directory.is_dir() or args.repetitions < 1:
        parser.error("provide a directory and at least one repetition")
    work = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(SOURCE.parent / "mod_resources.ls", work / "mod_resources.ls")
    shutil.copyfile(SOURCE.parent / "mod_publish.ls", work / "mod_publish.ls")
    cases = [("current", prepare("current", work=work))]
    if args.reference:
        cases.insert(0, ("reference", prepare("reference", args.reference, work=work)))
    elif args.reference_dir:
        cases.insert(0, ("reference", prepare("reference", source_dir=args.reference_dir, work=work)))
    entry_count = sum(not path.name.startswith(".") for path in directory.iterdir())
    fixture = work / "events.json"
    fixture.write_text(json.dumps({"name": "Finder directory latency", "viewport": {"width": 1000, "height": 650}, "events": [
        {"type": "click", "target": {"selector": "#" + args.view}},
        {"type": "click", "target": {"selector": "#go"}},
        {"type": "type", "target": {"selector": "#folder-path"}, "text": str(directory), "clear": True},
        {"type": "click", "target": {"selector": "#go-confirm"}},
        {"type": "assert_script", "js": f"Number(document.querySelector('#files .listing-window')?.getAttribute('data-total') || document.querySelectorAll('#files .file').length)==={entry_count}"},
        {"type": "click", "target": {"selector": "#files .file"}, "mods_str": "ctrl"},
        {"type": "assert_script", "js": "document.querySelectorAll('#files [aria-selected=true]').length===1"},
        {"type": "window_close"}, {"type": "assert_window_closed", "closed": True}
    ]}))
    results = []
    for iteration in range(args.repetitions):
        # Alternate ordering to reduce shared-host and filesystem warmup bias.
        for name, page in cases[::1 if iteration % 2 == 0 else -1]:
            tag = work / f"{name}-{iteration}"
            env = dict(os.environ, LAMBDA_LOG_LEVEL="ERROR", LAMBDA_LOG_FILE=str(tag) + "-native.log",
                       RADIANT_VIEW_TIMING_PATH=str(tag) + "-phases.tsv")
            with Path(str(tag) + ".log").open("w") as log:
                host = reference_binary if name == "reference" else binary
                replay = subprocess.run([str(host), "view", str(page), "--headless", "--event-file", str(fixture),
                                         "--event-result", str(tag) + ".json"], cwd=ROOT, env=env,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=180)
            outcome = json.loads(Path(str(tag) + ".json").read_text())
            if replay.returncode or outcome.get("result") != "PASS":
                raise SystemExit(f"Finder replay failed: {tag}.log")
            output = Path(str(tag) + ".log").read_text()
            def times(marker, selected):
                return [float(value) for value in re.findall(marker + " " + re.escape(str(directory)) + r" ([0-9.e+-]+) " + str(selected) + r"\b", output)]
            publish, frame = times("FINDER_PUBLISH", 0), times("FINDER_FRAME", 0)
            selection_publish, selection_frame = times("FINDER_PUBLISH", 1), times("FINDER_FRAME", 1)
            command = re.search(r"FINDER_COMMAND go-confirm ([0-9.e+-]+)", output)
            if not publish or not frame or not selection_publish or not selection_frame or not command:
                raise SystemExit(f"Finder timing markers missing: {tag}.log")
            record = {"version": name, "iteration": iteration,
                      "read_ms": float(command[1]), "publish_ms": sum(publish), "settle_ms": sum(frame),
                      "open_ms": float(command[1]) + sum(publish) + sum(frame),
                      "selection_ms": sum(selection_publish) + sum(selection_frame)}
            results.append(record)
            print(json.dumps(record), flush=True)
    summary = {name: {key: statistics.median(r[key] for r in results if r["version"] == name)
                      for key in ("read_ms", "publish_ms", "settle_ms", "open_ms", "selection_ms")}
               for name, _ in cases}
    (work / "results.json").write_text(json.dumps({"directory": str(directory), "binary": str(binary),
                                                "reference": args.reference or (str(args.reference_dir) if args.reference_dir else None),
                                                "view": args.view,
                                                "reference_binary": str(reference_binary),
                                                "repetitions": args.repetitions,
                                                "visible_entries": entry_count,
                                                "runs": results, "median": summary}, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
