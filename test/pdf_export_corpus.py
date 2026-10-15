"""Exercise the viewer's input routes and independently check exported PDFs.

Run from the repository root, for example:
  python3 test/pdf_export_corpus.py --root test/input --root test/html
  python3 test/pdf_export_corpus.py --root test/layout --jobs 4 --timeout 30
Artifacts and a resumable JSON report live under temp/. Poppler is required.
This checks serialization and rasterizability, not visual equivalence to a browser.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
import zlib

from pdf_output_check import validate


# Keep this in step with layout_format_routes and the graph/EPS/PDF dispatchers.
EXTENSIONS = set("html htm xhtml xht md markdown wiki docx epub xml tex latex pgf slides "
                 "svg png jpg jpeg gif mmd dot gv d2 structurizr dsl eps epsi pdf fo xslfo rpd "
                 "json yaml yml toml txt csv ini conf cfg log".split())
PAGED = {"fo", "xslfo", "rpd"}
EXCLUDED_DIRS = {"node_modules", "reference", "references", "expected", "baseline",
                 "baselines", "__pycache__", ".git"}


def command(args, timeout, output=None, env=None):
    """Kill the process group on timeout so child interpreters do not accumulate."""
    with subprocess.Popen(args, stdout=output or subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=env, start_new_session=True) as process:
        try:
            data, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
            raise
    return process.returncode, data.decode(errors="replace") if data else ""


def discover(roots, extensions):
    seen = set()
    for root in roots:
        for folder, dirs, files in os.walk(root, followlinks=False):
            dirs[:] = sorted(d for d in dirs if d not in EXCLUDED_DIRS)
            for name in sorted(files):
                path = Path(folder, name)
                if path.suffix.lower().lstrip(".") not in extensions:
                    continue
                if ".expected." in name or ".reference." in name or name.endswith(".layout.json"):
                    continue
                resolved = path.resolve()
                if resolved not in seen:
                    seen.add(resolved)
                    yield str(path)


def check(case, options, directory):
    source = Path(case["path"])
    extension = source.suffix.lower().lstrip(".")
    identifier = hashlib.sha256(json.dumps(case, sort_keys=True).encode()).hexdigest()[:16]
    stem = directory / identifier
    pdf = stem.with_suffix(".pdf")
    output = stem.with_suffix(".out")
    log = stem.with_suffix(".log")
    # A resumed test must not accept a PDF left by an earlier successful run.
    pdf.unlink(missing_ok=True)
    args = [options.exe, "render", str(source), "-vw", "640", "-vh", "480", "-o", str(pdf)]
    if case.get("paged"):
        args.append("--paged")
    env = dict(os.environ, LAMBDA_LOG_FILE=str(log), LAMBDA_LOG_LEVEL="ERROR",
               LAMBDA_HOME=str(options.lambda_home))
    result = dict(case, extension=extension, artifact=str(pdf), command=args,
                  executable_sha256=options.executable_sha256)
    started = time.monotonic()
    try:
        with output.open("wb") as stream:
            code, _ = command(args, options.timeout, stream, env)
        if "error_match" in case:
            diagnostics = "\n".join(p.read_text(errors="replace")
                                      for p in (output, log) if p.exists())
            assert code > 0, f"expected input rejection, got exit {code}"
            assert re.search(case["error_match"], diagnostics), "input rejection had the wrong diagnostic"
            assert not pdf.exists(), "rejected input left a PDF"
            result["status"] = "rejected"
            result["seconds"] = round(time.monotonic() - started, 3)
            return result
        assert code == 0, f"render exited {code}"
        assert pdf.is_file() and pdf.stat().st_size > 0, "render did not produce a PDF"
        code, info = command(["pdfinfo", str(pdf)], options.timeout)
        assert code == 0, "pdfinfo: " + info[-2000:]
        match = re.search(r"^Pages:\s+(\d+)", info, re.M)
        assert match, "pdfinfo returned no page count"
        pages = int(match[1])
        assert pages > 0, "empty page tree"
        result["pages"] = pages
        if "pages" in case:
            result["expected_pages"] = case["pages"]
            assert pages == case["pages"], f"expected {case['pages']} pages, got {pages}"
        # Unpaged PDF input is an intentional byte copy, potentially using a newer PDF version.
        if extension != "pdf" or case.get("paged"):
            validate(pdf, pages, case.get("box"), quiet=True)
        prefix = str(stem) + "_raster"
        code, raster_log = command(["pdftoppm", "-scale-to", "128", "-png", str(pdf), prefix],
                                   options.timeout)
        assert code == 0 and not raster_log.strip(), "pdftoppm: " + raster_log[-2000:]
        rasters = list(directory.glob(stem.name + "_raster-*.png"))
        assert len(rasters) == pages, f"rasterized {len(rasters)} of {pages} pages"
        for raster in rasters:
            raster.unlink()
        result["bytes"] = pdf.stat().st_size
        if not options.keep_pdfs:
            pdf.unlink()
        result["retained_pdf"] = options.keep_pdfs
        result["status"] = "pass"
    except (AssertionError, OSError, subprocess.TimeoutExpired, ValueError, IndexError,
            StopIteration, zlib.error) as error:
        result["status"] = "fail"
        result["error"] = str(error)
        result["diagnostics"] = "\n".join(p.read_text(errors="replace")[-6000:]
                                             for p in (output, log) if p.exists())
    if result["status"] == "pass":
        diagnostics = output.read_text(errors="replace")[-6000:]
        if diagnostics.strip():
            result["diagnostics"] = diagnostics
    result["seconds"] = round(time.monotonic() - started, 3)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", action="append", default=[])
    parser.add_argument("--manifest", type=Path, help="JSON list of {path, paged?, pages?, box?}")
    parser.add_argument("--expectations", type=Path, default=Path("test/pdf_export_expectations.json"))
    parser.add_argument("--extensions", help="comma separated extension filter")
    parser.add_argument("--output", type=Path, default=Path("temp/pdf_export_corpus"))
    parser.add_argument("--exe", default="./lambda.exe")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--resume", action="store_true", help="retain previous passing cases")
    parser.add_argument("--keep-pdfs", action="store_true", help="retain successful PDF artifacts")
    options = parser.parse_args()
    assert Path("temp").resolve() in options.output.resolve().parents, "artifacts must be under ./temp/"
    options.output.mkdir(parents=True, exist_ok=True)
    # Rebuilds replace lambda.exe; keep a stable host and package tree for this run.
    snapshot = options.output / "host"
    snapshot.mkdir(exist_ok=True)
    shutil.copy2(options.exe, snapshot / "lambda.exe")
    options.exe = str((snapshot / "lambda.exe").resolve())
    options.executable_sha256 = hashlib.sha256(Path(options.exe).read_bytes()).hexdigest()
    options.lambda_home = (snapshot / "lmd").resolve()
    if options.lambda_home.exists():
        shutil.rmtree(options.lambda_home)
    shutil.copytree(os.environ.get("LAMBDA_HOME", "lmd"), options.lambda_home)
    extensions = set(options.extensions.split(",")) if options.extensions else EXTENSIONS
    cases = json.loads(options.manifest.read_text()) if options.manifest else [
        {"path": path, "paged": Path(path).suffix.lower().lstrip(".") in PAGED}
        for path in discover(options.root or ["test/input", "test/html", "test/svg", "test/markup"], extensions)]
    expectations = json.loads(options.expectations.read_text())
    cases = [{**expectations.get(case["path"], {}), **case} for case in cases]
    if options.limit:
        cases = cases[:options.limit]
    report = options.output / "report.json"
    previous = json.loads(report.read_text()) if options.resume and report.exists() else []
    key = lambda entry: (entry["path"], bool(entry.get("paged")))
    retained = {key(entry): entry for entry in previous if entry["status"] in ("pass", "rejected")}
    for case in cases:
        entry = retained.get(key(case))
        if entry and (("pages" in case and entry.get("pages") != case["pages"]) or
                      ("error_match" in case and entry["status"] != "rejected")):
            del retained[key(case)]
    results = [retained[key(case)] for case in cases if key(case) in retained]
    todo = [case for case in cases if key(case) not in retained]
    print(f"PDF_CORPUS: {len(cases)} cases, {len(todo)} to run", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=options.jobs) as workers:
        futures = [workers.submit(check, case, options, options.output) for case in todo]
        last_update = time.monotonic()
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            if result["status"] == "fail":
                print(f"FAIL {result['path']}: {result['error']}", flush=True)
            if time.monotonic() - last_update > 20 or len(results) == len(cases):
                report.write_text(json.dumps(results, indent=2) + "\n")
                passed = sum(entry["status"] == "pass" for entry in results)
                rejected = sum(entry["status"] == "rejected" for entry in results)
                print(f"PDF_CORPUS: {len(results)}/{len(cases)} checked, {passed} exported, "
                      f"{rejected} correctly rejected", flush=True)
                last_update = time.monotonic()
    report.write_text(json.dumps(results, indent=2) + "\n")
    return int(any(entry["status"] == "fail" for entry in results))


if __name__ == "__main__":
    raise SystemExit(main())
