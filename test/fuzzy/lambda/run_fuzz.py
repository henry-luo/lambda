#!/usr/bin/env python3
"""Deterministic Lambda robustness and tier-differential fuzz runner.

The runner keeps expected-valid seed verification separate from arbitrary
mutations. A handled error is a useful result for arbitrary input, but never a
pass for a declared valid seed (D1.9, D1.10, D8.1.1v10).
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import random
import re
import sys
import time
from typing import Iterable, Optional, Union


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
INTERP_DIR = PROJECT_ROOT / "test" / "interp"
sys.path.insert(0, str(INTERP_DIR))

from lambda_process import LambdaProcessResult, run_lambda_process
from feature_inventory import check_inventory
from parser_differential import (
    check_coverage as check_parser_coverage,
    check_source as check_parser_source,
    check_stability as check_parser_stability,
    load_review_manifest,
    review_source,
    validate_reviews,
)
from source_generator import GeneratedSource, LambdaSourceGenerator
from source_mutator import mutate_invalid, mutate_valid


INTERP_SUMMARY_RE = re.compile(
    r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+)")
FATAL_OUTPUT_MARKERS = (
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "LeakSanitizer",
    "runtime error:",
    "root-witness: VIOLATION",
)
VALID_TIERS = ("interp", "jit", "auto")
SEED_ROLES = (
    "functional-valid",
    "procedural-valid",
    "syntax-invalid",
    "semantic-invalid",
    "module-root",
    "resource-scenario",
    "concurrency-scenario",
    "lifetime-scenario",
    "auto-promotion",
    "discovered-mutation",
)
TOKEN_FRAGMENTS = (
    "1", "0", "null", "true", "false", '"fuzz"', "[]", "{}",
    "let", "var", "fn", "pn", "view", "edit", "state", "on",
    "if", "else", "match", "case", "default", "for", "while",
    "raise", "apply", "where", "order", "group", "int", "string",
    "?", "^", "|>", "++", "**", ".?", "~~", "\\.", "...",
    "(", ")", "[", "]", "{", "}", ";", ",", ":", "=>",
)
INSERT_FRAGMENTS = (
    "let fuzz_value = [1, 2, 3]",
    "fn fuzz_identity(value) => value",
    "match 1 { case 1 => 1; default => 0 }",
    "[null, true, 42]",
    "{fuzz: 1}",
    "raise null",
)


@dataclass(frozen=True)
class Seed:
    source: Path
    role: str
    contract: str
    driver: str
    tiers: tuple[str, ...]
    deterministic: bool

    @property
    def name(self) -> str:
        return self.source.relative_to(PROJECT_ROOT).as_posix()

    @property
    def procedural(self) -> bool:
        return self.driver == "run"


@dataclass
class CaseResult:
    outcome: str
    message: str
    runs: list[tuple[str, LambdaProcessResult]]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run Lambda seed verification and deterministic fuzz cases.")
    parser.add_argument("--manifest", type=Path,
                        default=SCRIPT_DIR / "corpus_manifest.tsv")
    parser.add_argument("--executable", type=Path,
                        default=PROJECT_ROOT / "lambda.exe")
    parser.add_argument("--seed", type=int, default=0x4C414D42,
                        help="master RNG seed (default: 0x4C414D42)")
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="wall-clock fuzz budget after seed verification")
    parser.add_argument("--cases", type=int, default=0,
                        help="deterministic fuzz-case budget after seed verification")
    parser.add_argument("--shard-count", type=int, default=1,
                        help="deterministic worker-shard count for generated cases")
    parser.add_argument("--shard-index", type=int, default=0,
                        help="zero-based deterministic worker-shard index")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="per-invocation timeout in seconds")
    parser.add_argument("--mutation-tier", choices=VALID_TIERS, default="interp",
                        help="tier for arbitrary mutations and token input")
    parser.add_argument("--target", choices=("semantic", "parser"), action="append",
                        help="target to run; default is both semantic and parser")
    parser.add_argument("--parser-executable", type=Path,
                        default=PROJECT_ROOT / "lambda-cst.exe",
                        help="isolated grammar.js/C-parser verifier")
    parser.add_argument("--parser-review", type=Path,
                        default=SCRIPT_DIR / "parser_review.tsv")
    parser.add_argument("--coverage-corpus", type=Path,
                        help="temporary parser structural-coverage corpus directory")
    parser.add_argument("--max-parser-corpus", type=int, default=256,
                        help="maximum structural-novel parser inputs retained per run")
    parser.add_argument("--gc-stress", action="store_true",
                        help="compare each declared valid seed with forced GC/poison")
    parser.add_argument("--gc-force-every", type=int, default=1,
                        help="allocation interval for --gc-stress")
    parser.add_argument("--gc-seed", type=int,
                        help="optional deterministic sampled-GC seed")
    parser.add_argument("--gc-one-in", type=int,
                        help="optional sampled-GC denominator paired with --gc-seed")
    parser.add_argument("--root-witness", action="store_true",
                        help="enable the MIR Direct rooting witness during --gc-stress")
    parser.add_argument("--artifact-root", type=Path,
                        default=PROJECT_ROOT / "temp" / "lambda-fuzz")
    parser.add_argument("--max-output", type=int, default=256 * 1024,
                        help="maximum bytes retained per stdout/stderr artifact")
    parser.add_argument("--no-dry-run", dest="dry_run", action="store_false",
                        help="allow declared scenario I/O; disabled by default")
    parser.set_defaults(dry_run=True)
    parser.add_argument("--no-repo-seeds", action="store_true",
                        help="mutate only reviewed manifest success seeds")
    parser.add_argument("--verify-only", action="store_true",
                        help="run only the reviewed seed contracts")
    parser.add_argument("--skip-inventory", action="store_true",
                        help="skip the D1.10 feature-inventory ratchet")
    parser.add_argument("--replay", type=Path,
                        help="re-run the stored case in a fuzz artifact directory")
    parser.add_argument("--minimize", type=Path,
                        help="oracle-preservingly shrink a stored fuzz artifact")
    parser.add_argument("--minimize-attempts", type=int, default=128,
                        help="maximum replay attempts for --minimize")
    parser.add_argument("--list-seeds", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    if args.seconds < 0 or args.cases < 0 or args.timeout <= 0:
        parser.error("--seconds and --cases must be non-negative; --timeout must be positive")
    if args.shard_count <= 0 or not 0 <= args.shard_index < args.shard_count:
        parser.error("--shard-count must be positive and --shard-index must be in range")
    if args.gc_force_every <= 0:
        parser.error("--gc-force-every must be positive")
    if (args.gc_seed is None) != (args.gc_one_in is None):
        parser.error("--gc-seed and --gc-one-in must be supplied together")
    if args.gc_one_in is not None and args.gc_one_in <= 0:
        parser.error("--gc-one-in must be positive")
    if args.replay is not None and args.minimize is not None:
        parser.error("--replay and --minimize are mutually exclusive")
    if args.minimize_attempts <= 0:
        parser.error("--minimize-attempts must be positive")
    if args.max_parser_corpus <= 0:
        parser.error("--max-parser-corpus must be positive")
    args.manifest = args.manifest.resolve()
    args.executable = args.executable.resolve()
    args.parser_executable = args.parser_executable.resolve()
    args.parser_review = args.parser_review.resolve()
    args.artifact_root = args.artifact_root.resolve()
    args.coverage_corpus = (args.coverage_corpus or
                            args.artifact_root / "parser-corpus").resolve()
    args.targets = tuple(dict.fromkeys(args.target or ("semantic", "parser")))
    return args


def load_manifest(manifest: Path) -> list[Seed]:
    if not manifest.is_file():
        raise ValueError(f"missing corpus manifest: {manifest}")
    seeds: list[Seed] = []
    seen: set[Path] = set()
    for line_number, raw_line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 6:
            raise ValueError(f"{manifest}:{line_number}: expected 6 tab-separated fields")
        relative_name, role, contract, driver, tier_text, deterministic_text = fields
        if relative_name.startswith("@root/"):
            source = (PROJECT_ROOT / relative_name.removeprefix("@root/")).resolve()
        else:
            source = (SCRIPT_DIR / relative_name).resolve()
        if not source.is_file():
            raise ValueError(f"{manifest}:{line_number}: missing seed {relative_name}")
        if contract not in ("success", "reject"):
            raise ValueError(f"{manifest}:{line_number}: unknown contract {contract}")
        if role not in SEED_ROLES:
            raise ValueError(f"{manifest}:{line_number}: unknown seed role {role}")
        if driver not in ("direct", "run"):
            raise ValueError(f"{manifest}:{line_number}: unknown driver {driver}")
        tiers = tuple(tier_text.split(","))
        if not tiers or any(tier not in VALID_TIERS for tier in tiers):
            raise ValueError(f"{manifest}:{line_number}: invalid tier list {tier_text}")
        if deterministic_text not in ("true", "false"):
            raise ValueError(f"{manifest}:{line_number}: deterministic must be true or false")
        if source in seen:
            raise ValueError(f"{manifest}:{line_number}: duplicate seed {relative_name}")
        seen.add(source)
        seeds.append(Seed(source, role, contract, driver, tiers,
                          deterministic_text == "true"))
    if not seeds:
        raise ValueError(f"{manifest}: no seeds")
    return seeds


def discover_repository_seeds() -> list[Seed]:
    """Finds golden-backed scripts without promoting helpers/negatives to roots."""
    roots = (
        PROJECT_ROOT / "test" / "lambda",
        PROJECT_ROOT / "test" / "std",
    )
    seeds: list[Seed] = []
    for root in roots:
        if not root.is_dir():
            continue
        for source in sorted(root.rglob("*.ls")):
            if source.stat().st_size >= 50 * 1024:
                continue
            relative = source.relative_to(PROJECT_ROOT / "test" / "lambda") \
                if root.name == "lambda" else None
            if relative and "negative" in relative.parts:
                continue
            if not source.with_suffix(".txt").is_file():
                continue
            driver = "direct"
            if relative and relative.parts and relative.parts[0] in ("proc", "conc", "pdf"):
                driver = "run"
            seeds.append(Seed(source, "discovered-mutation", "success", driver,
                              ("interp",), False))
    return seeds


def invoke(args: argparse.Namespace, source: Path, tier: str,
           procedural: bool, extra_env: Optional[dict[str, str]] = None) -> LambdaProcessResult:
    environment = {"LC_ALL": "C", "TZ": "UTC"}
    if tier == "auto":
        # AUTO normally keeps counters quiet. Its stderr-only report proves this
        # row selected the policy rather than accidentally using eager JIT.
        environment["LAMBDA_RSS_REPORT"] = "1"
    if extra_env:
        environment.update(extra_env)
    return run_lambda_process(
        str(args.executable),
        str(source),
        tier,
        args.timeout,
        procedural=procedural,
        dry_run=args.dry_run,
        no_log=True,
        extra_env=environment,
    )


def result_problem(result: LambdaProcessResult) -> Optional[str]:
    combined_output = result.stdout + result.stderr
    if any(marker in combined_output for marker in FATAL_OUTPUT_MARKERS):
        return "sanitizer"
    if result.launch_error:
        return "infrastructure-error"
    if result.timed_out:
        return "timeout"
    if result.return_code is not None and result.return_code < 0:
        return "crash"
    return None


def verify_seed(args: argparse.Namespace, seed: Seed) -> CaseResult:
    runs: list[tuple[str, LambdaProcessResult]] = []
    for tier in seed.tiers:
        result = invoke(args, seed.source, tier, seed.procedural)
        runs.append((tier, result))
        problem = result_problem(result)
        if problem:
            return CaseResult(problem, f"{seed.name} tier={tier} {result.status}", runs)
        if seed.contract == "success" and result.status != "ok":
            return CaseResult("unexpected-reject",
                              f"{seed.name} tier={tier} {result.status}", runs)
        if seed.contract == "reject" and result.status == "ok":
            return CaseResult("unexpected-accept",
                              f"{seed.name} tier={tier} accepted", runs)

    if seed.contract == "reject":
        return CaseResult("expected-reject", seed.name, runs)

    interp_run = next((result for tier, result in runs if tier == "interp"), None)
    if interp_run is not None:
        summary = INTERP_SUMMARY_RE.search(interp_run.stderr)
        if summary is None:
            return CaseResult("infrastructure-error",
                              f"{seed.name} interp summary missing", runs)
        executed, fallback, _ = (int(value) for value in summary.groups())
        if executed == 0 or fallback != 0:
            return CaseResult("tier-fallback",
                              f"{seed.name} interp executed={executed} fallback={fallback}", runs)

    auto_run = next((result for tier, result in runs if tier == "auto"), None)
    if auto_run is not None:
        summary = INTERP_SUMMARY_RE.search(auto_run.stderr)
        if summary is None:
            return CaseResult("infrastructure-error",
                              f"{seed.name} auto summary missing", runs)
        executed, fallback, _ = (int(value) for value in summary.groups())
        if executed == 0 or fallback != 0:
            return CaseResult("auto-policy-failure",
                              f"{seed.name} auto executed={executed} fallback={fallback}", runs)

    if seed.deterministic and len(runs) > 1:
        baseline_tier, baseline = runs[0]
        for tier, result in runs[1:]:
            if result.stdout != baseline.stdout or result.status != baseline.status:
                return CaseResult(
                    "semantic-mismatch",
                    f"{seed.name} {baseline_tier}!={tier}",
                    runs,
                )
    return CaseResult("success", seed.name, runs)


def stress_environment(args: argparse.Namespace) -> dict[str, str]:
    """Returns one deterministic GC/rooting stress configuration (D8.6.3)."""
    environment = {
        "LAMBDA_GC_FORCE_EVERY": str(args.gc_force_every),
        "LAMBDA_GC_POISON_FREED": "1",
    }
    if args.gc_seed is not None:
        environment["LAMBDA_GC_FORCE_SEED"] = str(args.gc_seed)
        environment["LAMBDA_GC_FORCE_ONE_IN"] = str(args.gc_one_in)
    if args.root_witness:
        environment["LAMBDA_ROOT_WITNESS"] = "1"
    return environment


def verify_stress_seed(args: argparse.Namespace, seed: Seed,
                       baseline: CaseResult) -> CaseResult:
    """Requires GC-stressed rows to retain the baseline result exactly."""
    if baseline.outcome != "success":
        return baseline
    baseline_by_tier = dict(baseline.runs)
    runs: list[tuple[str, LambdaProcessResult]] = []
    for tier in seed.tiers:
        result = invoke(args, seed.source, tier, seed.procedural, stress_environment(args))
        runs.append((tier, result))
        problem = result_problem(result)
        if problem:
            return CaseResult(problem, f"GC stress {seed.name} tier={tier} {result.status}", runs)
        if result.status != "ok":
            return CaseResult("gc-unexpected-reject",
                              f"GC stress {seed.name} tier={tier} {result.status}", runs)
        if result.stdout != baseline_by_tier[tier].stdout:
            return CaseResult("gc-mismatch", f"GC stress output changed: {seed.name} tier={tier}", runs)
        if tier == "interp":
            summary = INTERP_SUMMARY_RE.search(result.stderr)
            if summary is None:
                return CaseResult("infrastructure-error",
                                  f"GC stress {seed.name} interp summary missing", runs)
            executed, fallback, _ = (int(value) for value in summary.groups())
            if executed == 0 or fallback != 0:
                return CaseResult("tier-fallback",
                                  f"GC stress {seed.name} executed={executed} fallback={fallback}", runs)
    return CaseResult("gc-success", seed.name, runs)


def verify_invalid_source(args: argparse.Namespace, source: Path) -> CaseResult:
    result = invoke(args, source, "interp", False)
    problem = result_problem(result)
    if problem:
        return CaseResult(problem, f"generated invalid {result.status}", [("interp", result)])
    if result.status == "ok":
        return CaseResult("unexpected-accept", "generated invalid source accepted", [("interp", result)])
    return CaseResult("expected-reject", "generated invalid source", [("interp", result)])


def verify_parser_source(args: argparse.Namespace, source: Path,
                         reviews: dict[Path, object], work_dir: Path) -> CaseResult:
    status = check_parser_source(args.parser_executable, source, work_dir, args.timeout)
    outcome, message = review_source(status, source, reviews)
    return CaseResult(outcome, message, [("parser", status.result)])


def verify_parser_stability(args: argparse.Namespace, source: Path,
                            work_dir: Path) -> CaseResult:
    result = check_parser_stability(args.parser_executable, source, work_dir, args.timeout)
    problem = result_problem(result)
    if problem:
        return CaseResult(problem, f"parser stability {result.status}", [("parser-stability", result)])
    if result.launch_error or result.return_code != 0:
        return CaseResult("parser-unstable", "parser fail-fast/recovery differed", [("parser-stability", result)])
    return CaseResult("parser-stable", "parser fail-fast/recovery stable", [("parser-stability", result)])


def mutate_source(source: str, rng: random.Random) -> tuple[str, str]:
    if not source:
        return rng.choice(INSERT_FRAGMENTS), "replace-empty"
    choice = rng.randrange(8)
    if choice == 0:
        at = rng.randrange(len(source))
        return source[:at] + source[at + 1:], "delete-character"
    if choice == 1:
        at = rng.randrange(len(source) + 1)
        return source[:at] + rng.choice(INSERT_FRAGMENTS) + source[at:], "insert-structure"
    if choice == 2:
        start = rng.randrange(len(source))
        end = rng.randrange(start + 1, min(len(source), start + 64) + 1)
        return source[:end] + source[start:end] + source[end:], "duplicate-span"
    if choice == 3:
        at = rng.randrange(len(source))
        return source[:at], "truncate-source"
    if choice == 4:
        return "(" + source + ")", "group-source"
    if choice == 5:
        return source + "\n" + rng.choice(INSERT_FRAGMENTS), "append-structure"
    if choice == 6:
        replacements = ((";", ""), ("}", "]"), (")", ""), ("=>", "^"))
        old, new = rng.choice(replacements)
        if old in source:
            return source.replace(old, new, 1), "replace-delimiter"
        return source + new, "append-delimiter"
    return "[" * 16 + source + "]" * 15, "unbalanced-nesting"


def generate_token_source(rng: random.Random) -> str:
    count = rng.randint(5, 48)
    return " ".join(rng.choice(TOKEN_FRAGMENTS) for _ in range(count)) + "\n"


def generate_parser_bytes(rng: random.Random) -> bytes:
    """Produces raw lexer/parser input, including invalid UTF-8 and NUL bytes."""
    source = bytearray(generate_token_source(rng).encode("utf-8"))
    for _ in range(rng.randint(1, 12)):
        source.insert(rng.randrange(len(source) + 1), rng.randrange(256))
    return bytes(source)


def mutate_parser_bytes(source: bytes, rng: random.Random) -> bytes:
    """Mutates retained structural coverage inputs without decoding raw bytes."""
    if not source:
        return generate_parser_bytes(rng)
    choice = rng.randrange(4)
    if choice == 0:
        at = rng.randrange(len(source))
        return source[:at] + source[at + 1:]
    if choice == 1:
        at = rng.randrange(len(source) + 1)
        return source[:at] + bytes((rng.randrange(256),)) + source[at:]
    if choice == 2:
        start = rng.randrange(len(source))
        end = rng.randrange(start + 1, min(len(source), start + 32) + 1)
        return source[:end] + source[start:end] + source[end:]
    return source[:max(1, len(source) // 2)]


def parser_corpus_sources(corpus: Path) -> list[Path]:
    if not corpus.is_dir():
        return []
    return sorted(path for path in corpus.glob("*.ls") if path.is_file())


def retain_parser_coverage(args: argparse.Namespace, source: Path,
                           signatures: set[str]) -> tuple[Optional[str], Optional[str]]:
    """Records novel C-parser shapes and exposes them to later byte mutations."""
    try:
        coverage = check_parser_coverage(
            args.parser_executable, source, args.artifact_root / "work", args.timeout)
    except ValueError as error:
        return None, str(error)
    signature = coverage.signature
    if signature in signatures:
        return signature, None
    signatures.add(signature)
    corpus_sources = parser_corpus_sources(args.coverage_corpus)
    if len(corpus_sources) < args.max_parser_corpus:
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()[:20]
        args.coverage_corpus.mkdir(parents=True, exist_ok=True)
        (args.coverage_corpus / f"{source_hash}.ls").write_bytes(source.read_bytes())
    return signature, None


def limit_text(text: str, max_output: int) -> str:
    data = text.encode("utf-8", errors="replace")
    if len(data) <= max_output:
        return text
    retained = data[:max_output].decode("utf-8", errors="replace")
    return retained + "\n[fuzz output truncated]\n"


def case_signature(result: CaseResult) -> str:
    """Keeps minimization tied to an oracle class, not a generic nonzero exit."""
    run_statuses = ",".join(f"{tier}:{run.status}" for tier, run in result.runs)
    if result.outcome.startswith("parser-"):
        detail = result.message.split(":", 1)[0]
    elif result.outcome == "semantic-mismatch":
        detail = result.message.rsplit(" ", 1)[-1]
    elif result.outcome in ("crash", "timeout", "sanitizer"):
        detail = run_statuses
    else:
        detail = result.outcome
    return f"{result.outcome}|{detail}|{run_statuses}"


def finding_signature(result: CaseResult) -> str:
    """Deduplicates findings by the oracle evidence, never by source hash alone."""
    if result.outcome == "semantic-mismatch":
        observations = ",".join(
            f"{tier}:{run.status}:{hashlib.sha256(run.stdout.encode('utf-8')).hexdigest()[:16]}"
            for tier, run in result.runs)
        return f"semantic-mismatch|{observations}"
    if result.outcome in ("crash", "sanitizer", "timeout"):
        observations = ",".join(f"{tier}:{run.status}" for tier, run in result.runs)
        markers = ",".join(
            marker for marker in FATAL_OUTPUT_MARKERS
            if any(marker in run.stdout + run.stderr for _, run in result.runs))
        return f"{result.outcome}|{observations}|{markers}"
    if result.outcome.startswith("parser-"):
        return f"{result.outcome}|{result.message.split(':', 1)[0]}"
    return case_signature(result)


def result_oracle(result: CaseResult) -> str:
    """Records the exact oracle so replay does not confuse parser checks."""
    if result.message.startswith("parser coverage "):
        return "parser-coverage"
    if result.runs:
        runner_name = result.runs[0][0]
        if runner_name == "parser-stability":
            return "parser-stability"
        if runner_name == "parser":
            return "parser-differential"
    return "semantic"


def artifact_paths(argument: Path) -> tuple[Path, Path]:
    resolved = argument.resolve()
    metadata = resolved if resolved.name == "metadata.json" else resolved / "metadata.json"
    case = metadata.parent / "case.ls"
    if not metadata.is_file() or not case.is_file():
        raise ValueError(f"artifact requires metadata.json and case.ls: {argument}")
    return metadata, case


def replay_case(args: argparse.Namespace, source: Path, metadata: dict,
                reviews: dict[Path, object], work_dir: Path) -> CaseResult:
    """Reconstructs the original target contract from saved artifact metadata."""
    expected = metadata.get("expected_outcome", metadata.get("outcome", ""))
    oracle = metadata.get("oracle", "")
    if oracle == "parser-coverage":
        try:
            coverage = check_parser_coverage(
                args.parser_executable, source, work_dir, args.timeout)
        except ValueError as error:
            return CaseResult("infrastructure-error", f"parser coverage {error}", [])
        return CaseResult("parser-coverage", coverage.signature,
                          [("parser-coverage", coverage.result)])
    if oracle == "parser-stability" or expected == "parser-unstable":
        return verify_parser_stability(args, source, work_dir)
    if oracle == "parser-differential" or expected.startswith("parser-") or \
            expected == "reviewed-parser-gap":
        return verify_parser_source(args, source, reviews, work_dir)
    case_kind = metadata.get("case_kind", "adversarial")
    if case_kind == "seed" or case_kind == "structured-valid":
        tiers = tuple(metadata.get("tiers", ("interp", "jit")))
        contract = metadata.get("contract", "success")
        driver = metadata.get("driver", "direct")
        role = metadata.get("role", "functional-valid")
        deterministic = metadata.get("deterministic", True)
        return verify_seed(args, Seed(source, role, contract, driver, tiers, deterministic))
    if case_kind == "structured-invalid":
        return verify_invalid_source(args, source)
    tier = metadata.get("tier", args.mutation_tier)
    original_tier = args.mutation_tier
    args.mutation_tier = tier
    try:
        return run_adversarial_case(args, source, "replay", metadata.get("case_ordinal", 0))
    finally:
        args.mutation_tier = original_tier


def reduce_source(args: argparse.Namespace, source_text: str, metadata: dict,
                  reviews: dict[Path, object], work_dir: Path,
                  wanted_signature: str) -> tuple[str, int]:
    """Hierarchically deletes lines then byte spans while preserving the oracle."""
    candidate_path = work_dir / "minimize-candidate.ls"
    attempts = 0

    def preserves(candidate: str) -> bool:
        nonlocal attempts
        if not candidate.strip() or attempts >= args.minimize_attempts:
            return False
        attempts += 1
        candidate_path.write_text(candidate, encoding="utf-8")
        return case_signature(replay_case(args, candidate_path, metadata, reviews, work_dir)) == wanted_signature

    current = source_text
    lines = current.splitlines(keepends=True)
    if len(lines) > 1:
        granularity = 2
        while len(lines) > 1 and attempts < args.minimize_attempts:
            chunk = max(1, (len(lines) + granularity - 1) // granularity)
            reduced = False
            for start in range(0, len(lines), chunk):
                candidate_lines = lines[:start] + lines[start + chunk:]
                candidate = "".join(candidate_lines)
                if preserves(candidate):
                    lines = candidate_lines
                    current = candidate
                    granularity = max(2, granularity - 1)
                    reduced = True
                    break
            if not reduced:
                if granularity >= len(lines):
                    break
                granularity = min(len(lines), granularity * 2)

    granularity = 2
    while len(current) > 1 and attempts < args.minimize_attempts:
        chunk = max(1, (len(current) + granularity - 1) // granularity)
        reduced = False
        for start in range(0, len(current), chunk):
            candidate = current[:start] + current[start + chunk:]
            if preserves(candidate):
                current = candidate
                granularity = max(2, granularity - 1)
                reduced = True
                break
        if not reduced:
            if granularity >= len(current):
                break
            granularity = min(len(current), granularity * 2)
    return current, attempts


def reduce_bytes(args: argparse.Namespace, source_bytes: bytes, metadata: dict,
                 reviews: dict[Path, object], work_dir: Path,
                 wanted_signature: str) -> tuple[bytes, int]:
    """Deletes raw byte spans without decoding a parser-only reproducer."""
    candidate_path = work_dir / "minimize-candidate.ls"
    attempts = 0

    def preserves(candidate: bytes) -> bool:
        nonlocal attempts
        if not candidate or attempts >= args.minimize_attempts:
            return False
        attempts += 1
        candidate_path.write_bytes(candidate)
        return case_signature(replay_case(args, candidate_path, metadata, reviews, work_dir)) == wanted_signature

    current = source_bytes
    granularity = 2
    while len(current) > 1 and attempts < args.minimize_attempts:
        chunk = max(1, (len(current) + granularity - 1) // granularity)
        reduced = False
        for start in range(0, len(current), chunk):
            candidate = current[:start] + current[start + chunk:]
            if preserves(candidate):
                current = candidate
                granularity = max(2, granularity - 1)
                reduced = True
                break
        if not reduced:
            if granularity >= len(current):
                break
            granularity = min(len(current), granularity * 2)
    return current, attempts


def write_artifact(args: argparse.Namespace, source_data: Union[str, bytes], kind: str,
                   metadata: dict, runs: Iterable[tuple[str, LambdaProcessResult]]) -> Path:
    source_bytes = source_data if isinstance(source_data, bytes) else source_data.encode(
        "utf-8", errors="surrogateescape")
    digest_input = kind + "\n" + metadata.get("finding_signature", kind)
    digest = hashlib.sha256(digest_input.encode("utf-8", errors="replace")).hexdigest()[:20]
    directory = args.artifact_root / kind / digest
    directory.mkdir(parents=True, exist_ok=True)
    stored_metadata = dict(metadata)
    stored_metadata.setdefault("expected_outcome", kind)
    stored_metadata["source_encoding"] = "raw-bytes" if isinstance(source_data, bytes) else "utf-8"
    stored_metadata["source_sha256"] = hashlib.sha256(source_bytes).hexdigest()
    stored_metadata["replay_command"] = (
        f"python3 test/fuzzy/lambda/run_fuzz.py --replay {directory}")
    if not (directory / "case.ls").exists():
        (directory / "case.ls").write_bytes(source_bytes)
        (directory / "metadata.json").write_text(
            json.dumps(stored_metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        for tier, result in runs:
            prefix = tier.replace("/", "_")
            (directory / f"{prefix}.stdout").write_text(
                limit_text(result.stdout, args.max_output), encoding="utf-8")
            (directory / f"{prefix}.stderr").write_text(
                limit_text(result.stderr, args.max_output), encoding="utf-8")
    else:
        occurrence = directory / f"occurrence-{stored_metadata['source_sha256'][:20]}.json"
        if not occurrence.exists():
            occurrence.write_text(json.dumps(stored_metadata, indent=2, sort_keys=True) + "\n",
                                  encoding="utf-8")
    return directory


def run_adversarial_case(args: argparse.Namespace, source: Path,
                         trace: str, case_index: int) -> CaseResult:
    result = invoke(args, source, args.mutation_tier, False)
    problem = result_problem(result)
    if problem:
        return CaseResult(problem, f"case={case_index} {trace} {result.status}",
                          [(args.mutation_tier, result)])
    if result.status == "ok":
        return CaseResult("accepted", f"case={case_index} {trace}",
                          [(args.mutation_tier, result)])
    return CaseResult("expected-reject", f"case={case_index} {trace}",
                      [(args.mutation_tier, result)])


def main() -> int:
    args = parse_args()
    if not args.executable.is_file():
        print(f"missing executable: {args.executable}", file=sys.stderr)
        return 2
    if "parser" in args.targets and not args.parser_executable.is_file():
        print(f"missing parser executable: {args.parser_executable}", file=sys.stderr)
        return 2
    if not args.skip_inventory:
        try:
            print(check_inventory())
        except ValueError as error:
            print(error, file=sys.stderr)
            return 2
    try:
        seeds = load_manifest(args.manifest)
        reviews = load_review_manifest(args.parser_review) if "parser" in args.targets else {}
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    if args.list_seeds:
        for seed in seeds:
            print(f"{seed.name}\t{seed.role}\t{seed.contract}\t{seed.driver}\t"
                  f"{','.join(seed.tiers)}\t{str(seed.deterministic).lower()}")
        return 0

    if args.replay is not None or args.minimize is not None:
        artifact_argument = args.replay or args.minimize
        try:
            metadata_path, source_path = artifact_paths(artifact_argument)
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            source_bytes = source_path.read_bytes()
            recorded_hash = metadata.get("source_sha256")
            current_hash = hashlib.sha256(source_bytes).hexdigest()
            if recorded_hash and recorded_hash != current_hash:
                raise ValueError("artifact case.ls no longer matches recorded source hash")
        except (OSError, ValueError, json.JSONDecodeError) as error:
            print(f"invalid fuzz artifact: {error}", file=sys.stderr)
            return 2
        work_dir = args.artifact_root / "work"
        result = replay_case(args, source_path, metadata, reviews, work_dir)
        expected_signature = metadata.get("expected_signature", "")
        observed_signature = case_signature(result)
        if args.replay is not None:
            print(f"replay outcome={result.outcome} signature={observed_signature}")
            if expected_signature and observed_signature != expected_signature:
                print(f"replay mismatch: expected={expected_signature}", file=sys.stderr)
                return 1
            if not expected_signature and result.outcome != metadata.get("expected_outcome"):
                print(f"replay mismatch: expected={metadata.get('expected_outcome')}", file=sys.stderr)
                return 1
            return 0
        if expected_signature and observed_signature != expected_signature:
            print("cannot minimize: the original oracle no longer reproduces", file=sys.stderr)
            return 1
        wanted_signature = expected_signature or observed_signature
        if metadata.get("source_encoding", "utf-8") == "raw-bytes":
            minimized, attempts = reduce_bytes(
                args, source_bytes, metadata, reviews, work_dir, wanted_signature)
        else:
            source_text = source_bytes.decode("utf-8")
            minimized, attempts = reduce_source(
                args, source_text, metadata, reviews, work_dir, wanted_signature)
        artifact_directory = metadata_path.parent
        minimized_bytes = minimized if isinstance(minimized, bytes) else minimized.encode("utf-8")
        (artifact_directory / "minimized.ls").write_bytes(minimized_bytes)
        (artifact_directory / "minimize.json").write_text(
            json.dumps({
                "attempts": attempts,
                "bytes_after": len(minimized_bytes),
                "bytes_before": len(source_bytes),
                "signature": wanted_signature,
            }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        crash_outcomes = {"crash", "sanitizer", "timeout", "parser-unstable"}
        promotion_target = ("test/lambda/negative/fuzzy_crashes/" if
                            metadata.get("expected_outcome") in crash_outcomes or
                            metadata.get("contract") == "reject" or
                            metadata.get("case_kind") == "structured-invalid" else
                            "test/lambda/")
        (artifact_directory / "promotion.md").write_text(
            "# Fuzz regression promotion review\n\n"
            f"- Oracle: `{wanted_signature}`\n"
            f"- Minimized source: `minimized.ls` ({len(minimized_bytes)} bytes)\n"
            f"- Suggested destination after root-cause review: `{promotion_target}`\n"
            "- Do not commit this artifact directly. Promote only a reproduced, "
            "root-caused regression; a successful `.ls` needs its matching `.txt`.\n\n"
            "This proposal is deliberately non-automatic under D1.9 and D1.10.\n",
            encoding="utf-8")
        print(f"minimized artifact={artifact_directory / 'minimized.ls'} attempts={attempts}")
        return 0

    print("Lambda fuzz runner")
    print(f"seed={args.seed} targets={','.join(args.targets)} dry_run={args.dry_run} "
          f"timeout={args.timeout:g}s shard={args.shard_index}/{args.shard_count}")
    print(f"manifest={args.manifest.relative_to(PROJECT_ROOT)} seeds={len(seeds)}")
    if args.gc_stress:
        print(f"gc_stress={stress_environment(args)}")
    counters: Counter[str] = Counter()
    failures = 0
    parser_coverage: set[str] = set()
    parser_coverage_novel = 0
    work_dir = args.artifact_root / "work"
    work_dir.mkdir(parents=True, exist_ok=True)

    if "parser" in args.targets:
        for outcome, message, status in validate_reviews(
                args.parser_executable, reviews, work_dir, args.timeout):
            counters[outcome] += 1
            if outcome != "reviewed-parser-gap":
                failures += 1
                print(f"parser review {outcome}: {message}")
            elif args.verbose:
                print(f"parser review: {message}")

    for seed in seeds:
        results: list[CaseResult] = []
        if "parser" in args.targets:
            results.append(verify_parser_source(args, seed.source, reviews, work_dir))
            results.append(verify_parser_stability(args, seed.source, work_dir))
            coverage_before = len(parser_coverage)
            _, coverage_error = retain_parser_coverage(args, seed.source, parser_coverage)
            if coverage_error:
                results.append(CaseResult(
                    "infrastructure-error", f"parser coverage {coverage_error}", []))
            elif len(parser_coverage) != coverage_before:
                parser_coverage_novel += 1
        if "semantic" in args.targets:
            semantic = verify_seed(args, seed)
            results.append(semantic)
            if args.gc_stress and semantic.outcome == "success":
                results.append(verify_stress_seed(args, seed, semantic))
        for result in results:
            counters[result.outcome] += 1
            green = ("success", "expected-reject", "parser-agreement", "parser-stable", "reviewed-parser-gap", "gc-success")
            if args.verbose or result.outcome not in green:
                print(f"seed {result.outcome}: {result.message}")
            if result.outcome not in green:
                failures += 1
                source_text = seed.source.read_text(encoding="utf-8", errors="replace")
                artifact = write_artifact(
                    args,
                    source_text,
                    result.outcome,
                    {
                        "case_kind": "seed",
                        "role": seed.role,
                        "contract": seed.contract,
                        "driver": seed.driver,
                        "message": result.message,
                        "seed": args.seed,
                        "source": seed.name,
                        "tiers": list(seed.tiers),
                        "deterministic": seed.deterministic,
                        "target": ",".join(args.targets),
                        "oracle": result_oracle(result),
                        "expected_signature": case_signature(result),
                        "finding_signature": finding_signature(result),
                    },
                    result.runs,
                )
                print(f"  artifact={artifact.relative_to(PROJECT_ROOT)}")

    if failures:
        print(f"seed verification failed: failures={failures} outcomes={dict(counters)}")
        return 1
    if args.verify_only:
        print(f"seed verification passed: outcomes={dict(counters)} "
              f"parser_structures={len(parser_coverage)} novel={parser_coverage_novel}")
        return 0

    mutation_sources: list[Path] = []
    if "semantic" in args.targets:
        mutation_sources = [seed.source for seed in seeds if seed.contract == "success"]
        if not args.no_repo_seeds:
            mutation_sources.extend(seed.source for seed in discover_repository_seeds())
        mutation_sources = sorted(set(mutation_sources))
        if not mutation_sources:
            print("no mutation sources", file=sys.stderr)
            return 2

    start = time.monotonic()
    case_count = 0
    case_ordinal = args.shard_index
    print(f"fuzz sources={len(mutation_sources)} tier={args.mutation_tier}")
    while True:
        if args.cases and case_count >= args.cases:
            break
        if args.seconds and time.monotonic() - start >= args.seconds:
            break
        if not args.cases and not args.seconds:
            break
        # Case-local RNG means each ordinal is replayable regardless of worker
        # scheduling or shard count (D1.10).
        case_seed = (args.seed << 32) ^ case_ordinal
        case_rng = random.Random(case_seed)
        generated: Optional[GeneratedSource] = None
        source_bytes: Optional[bytes] = None
        case_kind = "adversarial"
        if "semantic" not in args.targets and case_rng.randrange(2) == 0:
            coverage_sources = parser_corpus_sources(args.coverage_corpus)
            if coverage_sources and case_rng.randrange(2) == 0:
                original = case_rng.choice(coverage_sources)
                source_bytes = mutate_parser_bytes(original.read_bytes(), case_rng)
                trace = ("coverage-byte-mutation",)
                origin = f"coverage:{original.name}"
            else:
                source_bytes = generate_parser_bytes(case_rng)
                trace = ("raw-byte-sequence",)
                origin = "generated:raw-byte-sequence"
            case_kind = "parser-raw-bytes"
        elif "semantic" not in args.targets or case_rng.randrange(2) == 0:
            generated = LambdaSourceGenerator(case_rng).generate_valid()
            if case_rng.randrange(3) == 0:
                mutation = mutate_valid(generated.source, case_rng)
                source_text = mutation.source
                trace = generated.trace + (mutation.operation,)
            else:
                source_text = generated.source
                trace = generated.trace
            origin = f"generated:{generated.family}"
            case_kind = "structured-valid"
        elif case_rng.randrange(2) == 0:
            generated = LambdaSourceGenerator(case_rng).generate_invalid()
            if case_rng.randrange(2) == 0:
                mutation = mutate_invalid(generated.source, case_rng)
                source_text = mutation.source
                trace = generated.trace + (mutation.operation,)
            else:
                source_text = generated.source
                trace = generated.trace
            origin = f"generated:{generated.family}"
            case_kind = "structured-invalid"
        elif "semantic" in args.targets and case_rng.randrange(4) == 0:
            source_text = generate_token_source(case_rng)
            trace = ("token-sequence",)
            origin = "generated:token-sequence"
        else:
            original = case_rng.choice(mutation_sources)
            original_text = original.read_text(encoding="utf-8", errors="replace")
            source_text, mutation_name = mutate_source(original_text, case_rng)
            trace = (mutation_name,)
            origin = original.relative_to(PROJECT_ROOT).as_posix()
        case_source = work_dir / f"case_{case_ordinal:08d}.ls"
        if source_bytes is None:
            case_source.write_text(source_text, encoding="utf-8")
        else:
            case_source.write_bytes(source_bytes)
        results: list[CaseResult] = []
        if case_kind.startswith("structured"):
            if "parser" in args.targets:
                results.append(verify_parser_source(args, case_source, reviews, work_dir))
                results.append(verify_parser_stability(args, case_source, work_dir))
            if "semantic" in args.targets:
                if case_kind == "structured-valid":
                    generated_seed = Seed(case_source, "functional-valid", "success",
                                          "direct", ("interp", "jit"), True)
                    results.append(verify_seed(args, generated_seed))
                else:
                    results.append(verify_invalid_source(args, case_source))
        else:
            if "parser" in args.targets:
                results.append(verify_parser_stability(args, case_source, work_dir))
            if "semantic" in args.targets:
                results.append(run_adversarial_case(
                    args, case_source, ",".join(trace), case_ordinal))

        if "parser" in args.targets:
            coverage_before = len(parser_coverage)
            _, coverage_error = retain_parser_coverage(args, case_source, parser_coverage)
            if coverage_error:
                results.append(CaseResult(
                    "infrastructure-error", f"parser coverage {coverage_error}", []))
            elif len(parser_coverage) != coverage_before:
                parser_coverage_novel += 1

        green = ("accepted", "expected-reject", "success", "parser-agreement", "parser-stable", "reviewed-parser-gap")
        for result in results:
            counters[result.outcome] += 1
            if result.outcome not in green:
                failures += 1
                artifact = write_artifact(
                    args,
                    source_bytes if source_bytes is not None else source_text,
                    result.outcome,
                    {
                        "case_kind": case_kind,
                        "case_ordinal": case_ordinal,
                        "case_seed": case_seed,
                        "master_seed": args.seed,
                        "message": result.message,
                        "mutation": list(trace),
                        "origin": origin,
                        "target": ",".join(args.targets),
                        "oracle": result_oracle(result),
                        "tier": args.mutation_tier,
                        "tiers": ["interp", "jit"] if case_kind == "structured-valid" else [args.mutation_tier],
                        "driver": "direct",
                        "expected_signature": case_signature(result),
                        "finding_signature": finding_signature(result),
                    },
                    result.runs,
                )
                print(f"finding {result.outcome}: {result.message}")
                print(f"  artifact={artifact.relative_to(PROJECT_ROOT)}")
            elif args.verbose:
                print(f"case {result.outcome}: {result.message}")
        case_count += 1
        case_ordinal += args.shard_count

    elapsed = time.monotonic() - start
    print(f"fuzz complete: cases={case_count} elapsed={elapsed:.2f}s outcomes={dict(counters)} "
          f"parser_structures={len(parser_coverage)} novel={parser_coverage_novel}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
