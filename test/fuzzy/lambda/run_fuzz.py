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
from typing import Iterable, Optional


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
INTERP_DIR = PROJECT_ROOT / "test" / "interp"
sys.path.insert(0, str(INTERP_DIR))

from lambda_process import LambdaProcessResult, run_lambda_process


INTERP_SUMMARY_RE = re.compile(
    r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+)")
FATAL_OUTPUT_MARKERS = (
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
    "LeakSanitizer",
    "runtime error:",
)
VALID_TIERS = ("interp", "jit", "auto")
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
    contract: str
    driver: str
    tiers: tuple[str, ...]

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
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="per-invocation timeout in seconds")
    parser.add_argument("--mutation-tier", choices=VALID_TIERS, default="interp",
                        help="tier for arbitrary mutations and token input")
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
    parser.add_argument("--list-seeds", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    if args.seconds < 0 or args.cases < 0 or args.timeout <= 0:
        parser.error("--seconds and --cases must be non-negative; --timeout must be positive")
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
        if len(fields) != 4:
            raise ValueError(f"{manifest}:{line_number}: expected 4 tab-separated fields")
        relative_name, contract, driver, tier_text = fields
        source = (SCRIPT_DIR / relative_name).resolve()
        if not source.is_file():
            raise ValueError(f"{manifest}:{line_number}: missing seed {relative_name}")
        if contract not in ("success", "reject"):
            raise ValueError(f"{manifest}:{line_number}: unknown contract {contract}")
        if driver not in ("direct", "run"):
            raise ValueError(f"{manifest}:{line_number}: unknown driver {driver}")
        tiers = tuple(tier_text.split(","))
        if not tiers or any(tier not in VALID_TIERS for tier in tiers):
            raise ValueError(f"{manifest}:{line_number}: invalid tier list {tier_text}")
        if source in seen:
            raise ValueError(f"{manifest}:{line_number}: duplicate seed {relative_name}")
        seen.add(source)
        seeds.append(Seed(source, contract, driver, tiers))
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
            seeds.append(Seed(source, "success", driver, ("interp",)))
    return seeds


def invoke(args: argparse.Namespace, source: Path, tier: str,
           procedural: bool) -> LambdaProcessResult:
    return run_lambda_process(
        str(args.executable),
        str(source),
        tier,
        args.timeout,
        procedural=procedural,
        dry_run=args.dry_run,
        no_log=True,
        extra_env={"LC_ALL": "C", "TZ": "UTC"},
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

    if len(runs) > 1:
        baseline_tier, baseline = runs[0]
        for tier, result in runs[1:]:
            if result.stdout != baseline.stdout or result.status != baseline.status:
                return CaseResult(
                    "semantic-mismatch",
                    f"{seed.name} {baseline_tier}!={tier}",
                    runs,
                )
    return CaseResult("success", seed.name, runs)


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


def limit_text(text: str, max_output: int) -> str:
    data = text.encode("utf-8", errors="replace")
    if len(data) <= max_output:
        return text
    retained = data[:max_output].decode("utf-8", errors="replace")
    return retained + "\n[fuzz output truncated]\n"


def write_artifact(args: argparse.Namespace, source_text: str, kind: str,
                   metadata: dict, runs: Iterable[tuple[str, LambdaProcessResult]]) -> Path:
    digest_input = source_text + "\n" + json.dumps(metadata, sort_keys=True)
    digest = hashlib.sha256(digest_input.encode("utf-8", errors="replace")).hexdigest()[:20]
    directory = args.artifact_root / kind / digest
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "case.ls").write_text(source_text, encoding="utf-8")
    (directory / "metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for tier, result in runs:
        prefix = tier.replace("/", "_")
        (directory / f"{prefix}.stdout").write_text(
            limit_text(result.stdout, args.max_output), encoding="utf-8")
        (directory / f"{prefix}.stderr").write_text(
            limit_text(result.stderr, args.max_output), encoding="utf-8")
    return directory


def run_adversarial_case(args: argparse.Namespace, source: Path,
                         source_text: str, trace: str, case_index: int) -> CaseResult:
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
    try:
        seeds = load_manifest(args.manifest)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 2
    if args.list_seeds:
        for seed in seeds:
            print(f"{seed.name}\t{seed.contract}\t{seed.driver}\t{','.join(seed.tiers)}")
        return 0

    print("Lambda fuzz runner")
    print(f"seed={args.seed} dry_run={args.dry_run} timeout={args.timeout:g}s")
    print(f"manifest={args.manifest.relative_to(PROJECT_ROOT)} seeds={len(seeds)}")
    counters: Counter[str] = Counter()
    failures = 0

    for seed in seeds:
        result = verify_seed(args, seed)
        counters[result.outcome] += 1
        if args.verbose or result.outcome not in ("success", "expected-reject"):
            print(f"seed {result.outcome}: {result.message}")
        if result.outcome not in ("success", "expected-reject"):
            failures += 1
            source_text = seed.source.read_text(encoding="utf-8", errors="replace")
            artifact = write_artifact(
                args,
                source_text,
                result.outcome,
                {
                    "case_kind": "seed",
                    "contract": seed.contract,
                    "driver": seed.driver,
                    "message": result.message,
                    "seed": args.seed,
                    "source": seed.name,
                    "tiers": list(seed.tiers),
                },
                result.runs,
            )
            print(f"  artifact={artifact.relative_to(PROJECT_ROOT)}")

    if failures:
        print(f"seed verification failed: failures={failures} outcomes={dict(counters)}")
        return 1
    if args.verify_only:
        print(f"seed verification passed: outcomes={dict(counters)}")
        return 0

    mutation_sources = [seed.source for seed in seeds if seed.contract == "success"]
    if not args.no_repo_seeds:
        mutation_sources.extend(seed.source for seed in discover_repository_seeds())
    mutation_sources = sorted(set(mutation_sources))
    if not mutation_sources:
        print("no mutation sources", file=sys.stderr)
        return 2

    work_dir = args.artifact_root / "work"
    work_dir.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    start = time.monotonic()
    case_index = 0
    print(f"fuzz sources={len(mutation_sources)} tier={args.mutation_tier}")
    while True:
        if args.cases and case_index >= args.cases:
            break
        if args.seconds and time.monotonic() - start >= args.seconds:
            break
        if not args.cases and not args.seconds:
            break
        original = rng.choice(mutation_sources)
        if rng.randrange(4) == 0:
            source_text = generate_token_source(rng)
            trace = "token-sequence"
            origin = "generated"
        else:
            original_text = original.read_text(encoding="utf-8", errors="replace")
            source_text, trace = mutate_source(original_text, rng)
            origin = original.relative_to(PROJECT_ROOT).as_posix()
        case_source = work_dir / f"case_{case_index:08d}.ls"
        case_source.write_text(source_text, encoding="utf-8")
        result = run_adversarial_case(args, case_source, source_text, trace, case_index)
        counters[result.outcome] += 1
        if result.outcome not in ("accepted", "expected-reject"):
            failures += 1
            artifact = write_artifact(
                args,
                source_text,
                result.outcome,
                {
                    "case_kind": "adversarial",
                    "case_ordinal": case_index,
                    "master_seed": args.seed,
                    "message": result.message,
                    "mutation": trace,
                    "origin": origin,
                    "tier": args.mutation_tier,
                },
                result.runs,
            )
            print(f"finding {result.outcome}: {result.message}")
            print(f"  artifact={artifact.relative_to(PROJECT_ROOT)}")
        elif args.verbose:
            print(f"case {result.outcome}: {result.message}")
        case_index += 1

    elapsed = time.monotonic() - start
    print(f"fuzz complete: cases={case_index} elapsed={elapsed:.2f}s outcomes={dict(counters)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
