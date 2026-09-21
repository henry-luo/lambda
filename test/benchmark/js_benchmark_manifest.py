"""Shared construction rules for frozen JS benchmark source contracts."""

import hashlib
import os


MVP_V1_PROFILE = "mvp_v1"
TUNE14_V1_PROFILE = "tune14_v1"
FASTA_INPUT = "test/benchmark/beng/input/fasta_1000.txt"
PRETTIER_INPUT = "test/benchmark/text/prettier_ast.json"
NAVIER_STOKES_ID = "jetstream/navier_stokes"


def source_hash(path):
    with open(path, "rb") as stream:
        return hashlib.sha256(stream.read()).hexdigest()


def selected_source(row):
    source = row["js_path"] or row["ref_js"]
    if row["suite"] == "awfy":
        bundle = source.replace("2.js", "2_bundle.js")
        if os.path.exists(bundle):
            return bundle
    return source


def inputs_for(row):
    identifier = row["suite"] + "/" + row["name"]
    if identifier in {"beng/knucleotide", "beng/regexredux", "beng/revcomp"}:
        return [FASTA_INPUT]
    if identifier == "text/prettier_ast":
        return [PRETTIER_INPUT]
    return []


def navier_density_oracle(runner):
    oracle_id = runner["NAVIER_STOKES_DENSITY_ORACLE_V1"]
    trailer = runner["jetstream_post_timing_oracle"](oracle_id)
    return {
        "id": oracle_id,
        "timed_frames": 1,
        "post_timing_frames": 14,
        "source_checksum_frame": 15,
        "density_quantization": "floor(value * 1000)",
        "expected_stdout_text": runner["jetstream_post_timing_oracle_marker"](oracle_id),
        "trailer_sha256": hashlib.sha256(trailer.encode()).hexdigest(),
    }


def wrapper_for(runner, row, source, profile):
    if not row["is_jetstream"]:
        return {
            "kind": "direct_script",
            "strict_mode": "source_preserved",
            "timed_region": "source-defined __TIMING__ payload",
        }
    detected = runner["_detect_jetstream_run_function"](source)
    if detected is None:
        raise RuntimeError("JetStream wrapper detection failed for " + source)
    run_expression, repeats = detected
    canonical = "jetstream-runIteration-v1\n" + source + "\n" + run_expression + "\n" + str(repeats)
    wrapper = {
        "kind": "jetstream_run_iteration",
        "strict_mode": "source_preserved",
        "run_expression": run_expression,
        "repeat_count": repeats,
        "timed_region": "source-defined runIteration payload",
    }
    if profile == TUNE14_V1_PROFILE and row["suite"] + "/" + row["name"] == NAVIER_STOKES_ID:
        oracle = navier_density_oracle(runner)
        canonical += "\npost-timing-oracle\n" + oracle["id"] + "\n" + oracle["trailer_sha256"]
        wrapper["post_timing_oracle"] = oracle
        wrapper["timed_region"] = "one source-defined runIteration payload before post-timing validation"
    wrapper["canonical_sha256"] = hashlib.sha256(canonical.encode()).hexdigest()
    return wrapper


def correctness_oracle_for(runner, row, profile):
    identifier = row["suite"] + "/" + row["name"]
    if profile == TUNE14_V1_PROFILE and identifier == NAVIER_STOKES_ID:
        marker = navier_density_oracle(runner)["expected_stdout_text"]
        return ("source PASS/FAIL marker and zero exit status; source frame-15 checksum "
                "and post-timing full-density marker " + marker)
    return "source PASS/FAIL marker and zero exit status"


def build_workloads(runner, rows, profile):
    workloads = []
    for row in rows:
        source = selected_source(row)
        inputs = inputs_for(row)
        workloads.append({
            "id": row["suite"] + "/" + row["name"],
            "source": source,
            "sha256": source_hash(source),
            "inputs": [{"path": path, "sha256": source_hash(path)} for path in inputs],
            "arguments": [],
            "working_directory": ".",
            "wrapper": wrapper_for(runner, row, source, profile),
            "correctness_oracle": correctness_oracle_for(runner, row, profile),
        })
    return workloads
