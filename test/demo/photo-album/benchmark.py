"""Measure the photo album over the full 1000-photo corpus with a release host.

Standard library only. From the repository root, after _fetch.py and _thumbs.ls:

    python3 -I test/demo/photo-album/benchmark.py --binary release/lambda

The script copies the demo into temp/photo-album/bench/, instruments the copy's
handlers with clock() prints (the shipped app reads no clocks), and replays a
headless session: timeline scrolling, tile zoom steps and viewer paging. It
reports Lambda handler times per step, wall time per step (which also covers
Radiant layout and paint), and the host's peak resident memory for several
viewer-paging lengths (gap G2 in vibe/Lambda_Demo_Photo_Album.md).
"""
import argparse, json, os, re, resource, shutil, statistics, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "test/demo/photo-album"
WORK = ROOT / "temp/photo-album/bench"
CORPUS = os.environ.get("PHOTO_ALBUM_BENCH_CORPUS", "test/demo/photo-album")

# marker -> (text to find in photo-album.ls, replacement); each must match exactly once.
# *_AT lines carry the monotonic clock at handler entry: the gap to the next entry of the same
# kind is the whole step, including Radiant's layout and paint of the previous one. string()
# keeps six significant digits, so stamps are seconds modulo 100 (0.1 ms) and unwrapped below.
PROBES = {
    "activate": ("    session = publish(evt.target, relayout(evt.target, {*:session, active:true}))\n",
                 "    let probe_t0 = clock()\n"
                 "    session = publish(evt.target, relayout(evt.target, {*:session, active:true}))\n"
                 "    print(\"PA_ACTIVATE \" ++ string((clock() - probe_t0) * 1000) ++ \"\\n\")\n"),
    "scroll": ("    if (top != session.top) { session = mount(evt.target, {*:session, top:top}) }\n",
               "    let probe_t0 = clock()\n"
               "    print(\"PA_SCROLL_AT \" ++ string(probe_t0 - floor(probe_t0 / 100) * 100) ++ \"\\n\")\n"
               "    if (top != session.top) { session = mount(evt.target, {*:session, top:top}) }\n"
               "    print(\"PA_SCROLL \" ++ string((clock() - probe_t0) * 1000) ++ \"\\n\")\n"),
    "key": ("on keydown(evt) {\n",
            "on keydown(evt) {\n"
            "    let probe_now = clock()\n"
            "    print(\"PA_KEY_AT \" ++ string(probe_now - floor(probe_now / 100) * 100) ++ \"\\n\")\n"),
    "publish": ("pn publish(owner, st) {\n",
                "pn publish(owner, st) {\n"
                "    let probe_t0 = clock()\n"
                "    let probe_result = publish_inner(owner, st)\n"
                "    print(\"PA_PUBLISH \" ++ string((clock() - probe_t0) * 1000) ++ \"\\n\")\n"
                "    return probe_result\n"
                "}\n"
                "pn publish_inner(owner, st) {\n"),
    "dispatch": ("pn dispatch(owner, st, action) {\n",
                 "pn dispatch(owner, st, action) {\n"
                 "    let probe_t0 = clock()\n"
                 "    let probe_result = dispatch_inner(owner, st, action)\n"
                 "    print(\"PA_COMMAND \" ++ action ++ \" \" ++ string((clock() - probe_t0) * 1000) ++ \"\\n\")\n"
                 "    return probe_result\n"
                 "}\n"
                 "pn dispatch_inner(owner, st, action) {\n"),
}


def prepare():
    app = WORK / "app"
    if app.exists():
        shutil.rmtree(app)
    app.mkdir(parents=True)
    for path in SOURCE.iterdir():
        if path.suffix in (".ls", ".css") and not path.name.startswith("_"):
            shutil.copyfile(path, app / path.name)
    # the entry imports ~~.mod_resources and ~~.mod_publish from the demo collection's root
    shutil.copyfile(ROOT / "test/demo/mod_resources.ls", WORK / "mod_resources.ls")
    shutil.copyfile(ROOT / "test/demo/mod_publish.ls", WORK / "mod_publish.ls")
    entry = app / "photo-album.ls"
    source = entry.read_text()
    for name, (old, new) in PROBES.items():
        if source.count(old) != 1:
            raise SystemExit(f"benchmark: probe '{name}' no longer matches photo-album.ls")
        source = source.replace(old, new)
    entry.write_text(source)
    return entry


GRID_KEYS = 40
NOOP_KEYS = 12  # unhandled keys: the replay's own per-event cost, a baseline for the other steps


def events(scroll_steps, zoom_steps, viewer_steps):
    script = [{"type": "advance_time", "ms": 200}]
    for _ in range(scroll_steps):
        script += [{"type": "script", "js": "const p=document.getElementById('port');p.scrollTop+=700;p.dispatchEvent(new Event('scroll'))"},
                   {"type": "advance_time", "ms": 16}]
    script += [{"type": "script", "js": "const p=document.getElementById('port');p.scrollTop=2000;p.dispatchEvent(new Event('scroll'))"},
               {"type": "advance_time", "ms": 16}]
    for _ in range(NOOP_KEYS):
        script += [{"type": "key_press", "key": "q"}, {"type": "advance_time", "ms": 16}]
    for key in ["="] * zoom_steps + ["-"] * zoom_steps:
        script += [{"type": "key_press", "key": key}, {"type": "advance_time", "ms": 16}]
    # grid focus moves: the first press focuses the first photo, the rest walk the timeline
    for _ in range(GRID_KEYS):
        script += [{"type": "key_press", "key": "right"}, {"type": "advance_time", "ms": 16}]
    if viewer_steps:
        # open the viewer on a tile that is inside the viewport, so the double-click lands on it
        script += [{"type": "script", "js": "const p=document.getElementById('port').getBoundingClientRect();"
                    "const t=Array.from(document.querySelectorAll('#grid .tile')).find(e=>{const r=e.getBoundingClientRect();return r.y>=p.y&&r.y+r.height<=p.y+p.height});"
                    "t.setAttribute('data-bench','1')"},
                   {"type": "dblclick", "target": {"selector": "[data-bench='1']"}},
                   {"type": "assert_attribute", "target": {"selector": "#album"}, "attribute": "data-viewer", "equals": "true"}]
        for _ in range(viewer_steps):
            script += [{"type": "key_press", "key": "right"}, {"type": "advance_time", "ms": 16}]
    script.append({"type": "assert_script", "js": "document.querySelectorAll('#grid .tile').length>0"})
    return {"name": "photo album benchmark", "html": "", "viewport": {"width": 1440, "height": 900}, "events": script}


def run(binary, entry, plan, label):
    path = WORK / f"{label}.json"
    path.write_text(json.dumps(dict(plan["events"], html=str(entry.relative_to(ROOT)))))
    env = dict(os.environ, PHOTO_ALBUM_DIR=CORPUS)
    before = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    started = time.perf_counter()
    result = subprocess.run([binary, "view", str(entry.relative_to(ROOT)), "--headless", "--event-file", str(path)],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=1800)
    wall = time.perf_counter() - started
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    output = result.stdout + result.stderr
    if "Result: PASS" not in output:
        raise SystemExit(f"benchmark: {label} replay failed\n{output[-2500:]}")
    samples = {}
    for kind, value in re.findall(r"PA_(ACTIVATE|SCROLL|PUBLISH|COMMAND [a-z-]+) ([0-9.]+)", output):
        samples.setdefault(kind, []).append(float(value))
    # per-step wall time: gaps between consecutive handler entries of one phase
    def gaps(stamps):
        return [((b - a) % 100.0) * 1000.0 for a, b in zip(stamps, stamps[1:])]
    scrolls = [float(v) for v in re.findall(r"PA_SCROLL_AT ([0-9.]+)", output)]
    keys = [float(v) for v in re.findall(r"PA_KEY_AT ([0-9.]+)", output)]
    noop_keys = keys[:NOOP_KEYS]
    zoom_keys = keys[NOOP_KEYS:NOOP_KEYS + 2 * plan["zoom_steps"]]
    grid_keys = keys[NOOP_KEYS + 2 * plan["zoom_steps"]:NOOP_KEYS + 2 * plan["zoom_steps"] + GRID_KEYS]
    viewer_keys = keys[NOOP_KEYS + 2 * plan["zoom_steps"] + GRID_KEYS:]
    samples["step baseline"] = gaps(noop_keys)
    samples["step scroll"] = gaps(scrolls[:plan["scroll_steps"]])
    samples["step zoom"] = gaps(zoom_keys)
    samples["step grid-key"] = gaps(grid_keys)
    samples["step viewer"] = gaps(viewer_keys)
    # macOS reports ru_maxrss in bytes, Linux in KiB; RUSAGE_CHILDREN keeps the largest child so far
    scale = 1 if sys.platform == "darwin" else 1024
    return {"wall_s": wall, "peak_mb": max(peak, before) * scale / 1e6, "samples": samples}


def summary(values):
    ordered = sorted(values)
    return {"n": len(values), "median_ms": round(statistics.median(values), 2),
            "p95_ms": round(ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))], 2), "max_ms": round(max(values), 2)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--binary", default="release/lambda")
    parser.add_argument("--scroll", type=int, default=60)
    parser.add_argument("--zoom", type=int, default=4)
    parser.add_argument("--viewer", type=int, nargs="+", default=[0, 100, 300])
    args = parser.parse_args()
    # PHOTO_ALBUM_BENCH_CORPUS may name a manifest-only copy to time the grid without image decoding
    if not (ROOT / CORPUS / "photos.json").exists():
        raise SystemExit("benchmark: no photos.json in " + CORPUS)
    entry = prepare()
    report = {"binary": args.binary, "runs": []}
    for steps in args.viewer:
        # each length runs in its own process so RUSAGE_CHILDREN's peak belongs to it alone
        child = subprocess.run([sys.executable, "-I", __file__, "--binary", args.binary, "--scroll", str(args.scroll),
                                "--zoom", str(args.zoom), "--single", str(steps)], cwd=ROOT, capture_output=True, text=True)
        if child.returncode != 0:
            raise SystemExit(child.stdout + child.stderr)
        report["runs"].append(json.loads(child.stdout))
    print(json.dumps(report, indent=2))


def single(binary, scroll, zoom, steps):
    entry = WORK / "app/photo-album.ls"
    plan = {"events": events(scroll, zoom, steps), "scroll_steps": scroll, "zoom_steps": zoom}
    measured = run(binary, entry, plan, f"run-{steps}")
    out = {"viewer_steps": steps, "wall_s": round(measured["wall_s"], 2), "peak_mb": round(measured["peak_mb"], 1)}
    for kind, values in sorted(measured["samples"].items()):
        if values:
            out[kind.lower().replace(" ", "_")] = summary(values)
    print(json.dumps(out))


if __name__ == "__main__":
    if "--single" in sys.argv:
        at = sys.argv.index("--single")
        options = dict(zip(sys.argv[1:at:2], sys.argv[2:at:2]))
        single(options["--binary"], int(options["--scroll"]), int(options["--zoom"]), int(sys.argv[at + 1]))
    else:
        main()
