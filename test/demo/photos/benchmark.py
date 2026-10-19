#!/usr/bin/env python3
"""Measure the initial layered editor using an optimized, explicitly identified host."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

try:
    import resource
except ImportError:
    resource = None

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "lambda.exe")
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--width", type=int, default=1024)
    parser.add_argument("--height", type=int, default=768)
    parser.add_argument("--layers", type=int, default=8)
    parser.add_argument("--moves", type=int, default=16)
    parser.add_argument("--output", type=Path, default=ROOT / "temp/photos/performance/results.json")
    args = parser.parse_args()
    if min(args.iterations, args.width, args.height, args.layers, args.moves) < 1:
        parser.error("counts and dimensions must be positive")
    binary = args.binary.resolve()
    binary_bytes = binary.read_bytes()
    # Developer Guide §7 uses this debug-only validator trace to identify the host.
    if b"VALIDATOR] validate_against_base_type" in binary_bytes:
        parser.error("use an optimized host: make build-release-compile (never make release in a worktree)")
    scratch = ROOT / "temp/photos/performance"
    scratch.mkdir(parents=True, exist_ok=True)
    scenario = f'''import editor: lambda.editor.raster
import img: lambda.image
pn main() {{
    let began = clock()
    let raster = img.blank({{width:{args.width},height:{args.height},color:[0.2,0.5,0.8,0.7]}})^
    var st = editor.open_session(editor.new_document({{width:{args.width},height:{args.height}}})^ )^
    for (i in 1 to {args.layers}) {{
        st = editor.exec(st,{{id:"layer.insert",raster:raster,name:"Layer " ++ string(i),position:[i,i]}})^
    }}
    let constructed = clock()
    for (i in 1 to {args.moves}) {{ st = editor.exec(st,{{id:"layer.move",dx:1,dy:0}})^ }}
    let moved = clock()
    let bytes = img.encode(st.doc,{{format:'psd'}})^
    let encoded = clock()
    print(format({{construct_ms:(constructed-began)*1000,move_ms:(moved-constructed)*1000,
        encode_ms:(encoded-moved)*1000,output_bytes:len(bytes),layers:len(editor.layers(st.doc))}},'json'))
    return null
}}
'''
    script = scratch / "_layered.ls"
    script.write_text(scenario)
    env = dict(os.environ, LAMBDA_HOME=str(ROOT / "lmd"))
    samples = []
    for _ in range(args.iterations):
        began = time.monotonic()
        run = subprocess.run([str(binary), "run", str(script)], cwd=ROOT, env=env,
                             capture_output=True, text=True, check=True)
        sample = json.loads(run.stdout)
        sample["wall_ms"] = (time.monotonic() - began) * 1000
        samples.append(sample)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    package_hash = hashlib.sha256()
    for path in sorted((ROOT / "lmd/package").rglob("*.ls")):
        package_hash.update(path.relative_to(ROOT).as_posix().encode())
        package_hash.update(path.read_bytes())
    peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss if resource else None
    report = {
        "profile": "release", "binary": str(binary),
        "binary_sha256": hashlib.sha256(binary_bytes).hexdigest(), "revision": revision,
        "package_sha256": package_hash.hexdigest(), "scenario_sha256": hashlib.sha256(scenario.encode()).hexdigest(),
        "platform": platform.platform(), "machine": platform.machine(),
        "cpu_count": os.cpu_count(), "execution_backend": env.get("LAMBDA_EXEC_BACKEND", "auto"),
        "runtime_environment": {key: env[key] for key in ("LAMBDA_GC_FORCE_EVERY", "LAMBDA_GC_POISON_FREED", "LAMBDA_DISABLE_MIR_CACHE") if key in env},
        "scenario": {key: getattr(args, key) for key in ("width", "height", "layers", "moves", "iterations")},
        "samples": samples,
        "median_ms": {key: statistics.median(sample[key] for sample in samples)
                      for key in ("construct_ms", "move_ms", "encode_ms", "wall_ms")},
        "peak_child_rss_bytes": None if peak is None else peak if platform.system() == "Darwin" else peak * 1024,
        "notes": ["First process is cold; subsequent processes may reuse filesystem/compiler caches.",
                  "Move timings include eager validation/composition and snapshot history.",
                  "Peak RSS is the maximum child-process peak across samples, not live raster bytes.",
                  "This headless scenario does not measure canvas upload or input-to-display latency."],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"report": str(args.output), "median_ms": report["median_ms"],
                      "peak_child_rss_bytes": report["peak_child_rss_bytes"]}, indent=2))


if __name__ == "__main__":
    main()
