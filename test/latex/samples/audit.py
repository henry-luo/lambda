#!/usr/bin/env python3
"""Record all original samples by target; isolate CLI failures and keep their evidence."""
import argparse
import concurrent.futures
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def audit_one(binary, directory, sample, target, export):
    stem = f"{sample}_{target}"
    script = directory / f"{stem}.ls"
    script.write_text(
        "import audit: ~~.~~.~~.test.latex.samples.audit\n"
        "pub pn main() {\n"
        f" let result = audit.record({json.dumps(sample)}, {json.dumps(target)}, true)\n"
        " print(format(result, \"json\") ^ { \"{}\" })\n}\n"
    )
    log = directory / f"{stem}.log"
    with log.open("w") as stderr:
        process = subprocess.run([str(binary), "run", "--no-log", str(script)], cwd=ROOT,
                                 stdout=subprocess.PIPE, stderr=stderr, text=True)
    raw = directory / f"{stem}.json"
    raw.write_text(process.stdout)
    if process.returncode:
        return {"file": sample, "target": target, "cli_status": process.returncode,
                "log": str(log.relative_to(ROOT)), "parsed": False}
    try:
        record = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        return {"file": sample, "target": target, "cli_status": process.returncode,
                "log": str(log.relative_to(ROOT)), "parsed": False,
                "output_error": str(error)}
    markup = record.pop("html", None)
    if markup:
        html_file = directory / f"{stem}.html"
        html_file.write_text(markup)
        if export and target in ("svg", "pdf"):
            artifact = directory / f"{stem}.{target}"
            with log.open("a") as stderr:
                result = subprocess.run([str(binary), "render", "--no-log", str(html_file), "-o", str(artifact)],
                                        cwd=ROOT, stdout=stderr, stderr=stderr)
            record["export_status"] = result.returncode
            record["artifact_bytes"] = artifact.stat().st_size if artifact.exists() else 0
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default="./lambda.exe")
    parser.add_argument("--target", choices=("html", "svg", "pdf", "all"), default="all")
    parser.add_argument("--sample", action="append")
    parser.add_argument("--export", action="store_true", help="also invoke native SVG/PDF exporters")
    parser.add_argument("--jobs", type=int, default=3)
    args = parser.parse_args()
    binary = (ROOT / args.binary).resolve()
    directory = ROOT / "temp/latex_phase3/audits"
    directory.mkdir(parents=True, exist_ok=True)
    samples = args.sample or sorted(path.stem for path in (ROOT / "test/latex/samples").glob("*.tex"))
    targets = ("html", "svg", "pdf") if args.target == "all" else (args.target,)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(audit_one, binary, directory, sample, target, args.export)
                   for sample in samples for target in targets]
        records = []
        for future in concurrent.futures.as_completed(futures):
            record = future.result()
            records.append(record)
            print(f"{record['file']} {record['target']}: parsed={record['parsed']} "
                  f"unsupported={record.get('unsupported', '?')} "
                  f"serialized={record.get('output_chars', 0) > 0 and not record.get('serialization_error')} "
                  f"export={record.get('export_status', 'not requested')}", flush=True)
    records.sort(key=lambda record: (record["file"], record["target"]))
    suffix = "_selected" if args.sample else ""
    output = directory / f"summary_{args.target}{suffix}.json"
    output.write_text(json.dumps(records, indent=2, ensure_ascii=False) + "\n")
    return int(any(not record["parsed"] or record.get("serialization_error") or
                   not record.get("output_chars", 0) or
                   record.get("export_status", 0) != 0 or
                   (args.export and record["target"] in ("svg", "pdf") and
                    not record.get("artifact_bytes", 0)) for record in records))


if __name__ == "__main__":
    raise SystemExit(main())
