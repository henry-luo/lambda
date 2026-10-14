#!/usr/bin/env python3
"""Measure the photo pipeline in a release host; generated inputs stay in temp/."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / "temp/photo/performance"
SCRIPT = """import pipeline: ~~.~~.~~.test.demo.photo.mod_pipeline
import model: ~~.~~.~~.test.demo.photo.mod_model
@IMPORTS@
pn measure_once(image,recipe,source,case_name) {
    let began = clock()
    let pixels = (@EXTRA_CASES@if (case_name == "encode") (
        let encoded=save(image,"temp/photo/performance/_benchmark_encode.png")^, image
    ) else if (case_name == "histogram") histogram(as_float(grayscale(image)),32)
    else if (case_name == "blur") blur(image,15) else pipeline.evaluate(image,recipe))
    if (pixels is error) { raise pixels }
    let elapsed = (clock()-began)*1000
    return elapsed
}
pn main() {
    let env = sys.proc.self.env
    let w = int(env.PHOTO_BENCH_WIDTH)
    let h = int(env.PHOTO_BENCH_HEIGHT)
    let sample = as_ubyte([[[32,64,96,255],[128,192,224,128]],[[240,128,64,255],[16,32,48,0]]])
    let image = resize(sample,h,w)
    let base = model.recipe()
    let base_recipe = if (env.PHOTO_BENCH_CASE == "color" or env.PHOTO_BENCH_CASE == "composition") {*:base,exposure:0.4,contrast:1.12,saturation:1.2,gamma:1.15,preset:"warm"}
        else if (env.PHOTO_BENCH_CASE == "effects") {*:base,softness:6.0,sharpen:0.8,vignette:0.5}
        else if (env.PHOTO_BENCH_CASE == "geometry") {*:base,angle:3.0,crop:{left:0.1,top:0.1,right:0.9,bottom:0.9}}
        else base
    @RECIPE@
    let source={path:"file:///photo-benchmark-source.png",pixels:image,width:w,height:h,identity:null}
    var index = 0
    while (index < int(env.PHOTO_BENCH_ITERATIONS)) {
        let elapsed=measure_once(image,recipe,source,env.PHOTO_BENCH_CASE)
        print("PHOTO_TIME:" ++ string(elapsed) ++ ":done\\n")
        index = index+1
    }
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./lambda.exe")
    parser.add_argument("--output", default="temp/photo/performance/results.json")
    parser.add_argument("--sizes", nargs="+", default=["960x640", "1280x853", "4000x3000", "4000x4000"])
    parser.add_argument("--cases", nargs="+", choices=["neutral", "color", "effects", "geometry", "blur", "encode", "histogram", "composition"],
                        default=["neutral", "color", "effects", "geometry", "blur"])
    parser.add_argument("--reference-revision", help="read the original Lambda evaluator from this Git revision")
    parser.add_argument("--iterations", type=int, default=7)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("iterations must be positive")
    binary = Path(args.binary).resolve()
    # The release host strips the validator trace used to identify debug hosts.
    if b"VALIDATOR] validate_against_base_type" in binary.read_bytes():
        parser.error("use make release before timing; this host contains debug traces")
    WORK.mkdir(parents=True, exist_ok=True)
    script = WORK / "_benchmark.ls"
    source = SCRIPT
    imports, recipe, extra = "", "let recipe=base_recipe", ""
    if args.reference_revision:
        if "composition" in args.cases:
            parser.error("composition uses the current export path; benchmark it without --reference-revision")
        reference = WORK / "reference"
        reference.mkdir(exist_ok=True)
        for name in ["mod_pipeline.ls", "mod_geometry.ls", "mod_presets.ls"]:
            data = subprocess.check_output(["git", "show", f"{args.reference_revision}:test/demo/photo/{name}"], cwd=ROOT)
            (reference / name).write_bytes(data)
        source = source.replace("pipeline: ~~.~~.~~.test.demo.photo.mod_pipeline", "pipeline: .reference.mod_pipeline")
    else:
        imports = "import files: ~~.~~.~~.test.demo.photo.mod_io\nimport layers: ~~.~~.~~.test.demo.photo.mod_layers"
        recipe = '''let recipe=if (env.PHOTO_BENCH_CASE=="composition") {*:base_recipe,
        layers:[layers.create(base_recipe,"text","Photo benchmark",w/h)]} else base_recipe'''
        extra = '''if (case_name=="composition") (
            let exported=files.export_png("temp/photo/performance/_benchmark_composition.png",source,recipe)^,image
        ) else '''
    script.write_text(source.replace("@IMPORTS@", imports).replace("@RECIPE@", recipe).replace("@EXTRA_CASES@", extra))
    report = {"machine": platform.platform(), "processor": platform.processor(),
              "cpus": os.cpu_count(), "binary": str(binary),
              "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "git": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "reference_revision": args.reference_revision,
              "date": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "results": []}
    output = ROOT / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    for size in args.sizes:
        width, height = map(int, size.split("x"))
        if width < 1 or height < 1 or width * height > 16000000:
            parser.error("sizes must be positive and at most 16 megapixels")
        for case in args.cases:
            env = {**os.environ, "PHOTO_BENCH_WIDTH": str(width), "PHOTO_BENCH_HEIGHT": str(height),
                   "PHOTO_BENCH_CASE": case, "PHOTO_BENCH_ITERATIONS": str(args.iterations)}
            command = [str(binary), "run", str(script), "--no-log"]
            runner = "import resource,subprocess,sys; r=subprocess.run(sys.argv[1:],capture_output=True,text=True); sys.stdout.write(r.stdout); sys.stderr.write(r.stderr); print(str(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)+\" maximum resident set size\",file=sys.stderr); sys.exit(r.returncode)"
            command = [sys.executable, "-c", runner, *command]
            entry = {"size": size, "case": case}
            try:
                process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                           stderr=subprocess.PIPE, text=True, start_new_session=True)
                stdout, stderr = process.communicate(timeout=args.timeout)
                result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
                samples = [float(x) for x in re.findall(r"PHOTO_TIME:([0-9.eE+-]+):", result.stdout)]
                entry.update(exit_code=result.returncode, samples_ms=samples)
                if samples:
                    warm = sorted(samples[1:] or samples)
                    entry.update(cold_ms=samples[0], p50_ms=statistics.median(warm),
                                 p95_ms=warm[min(len(warm)-1, int(len(warm)*0.95))])
                rss = re.search(r"(\d+)\s+maximum resident set size", result.stderr)
                if rss:
                    entry["peak_rss_bytes"] = int(rss[1]) * (1 if platform.system() == "Darwin" else 1024)
                if result.returncode or len(samples) != args.iterations:
                    entry["error"] = (result.stderr + result.stdout)[-2500:] or "missing timing samples"
            except subprocess.TimeoutExpired:
                # terminate the runner and host together, including a timed-out reference evaluator.
                if os.name == "posix":
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
                process.communicate()
                entry["error"] = "timeout"
            report["results"].append(entry)
            output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(entry), flush=True)
    return int(any("error" in entry for entry in report["results"]))


if __name__ == "__main__":
    raise SystemExit(main())
