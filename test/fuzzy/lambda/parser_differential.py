"""Tree-sitter/C-parser acceptance comparison with reviewed-gap ratcheting.

`grammar.js` is Lambda's first structural check, not a language authority.
This adapter deliberately reports both directions of a difference so each one
is adjudicated against D8.1.2v3 rather than silently choosing the C parser.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
INTERP_DIR = PROJECT_ROOT / "test" / "interp"
sys.path.insert(0, str(INTERP_DIR))

from lambda_process import LambdaProcessResult, run_process


SUMMARY_RE = re.compile(
    r"total=(\d+)\s+ts_ok=(\d+)\s+rd_ok=(\d+)\s+missing=(\d+)\s+extra=(\d+)")
STABILITY_RE = re.compile(r"total=(\d+)\s+unstable=(\d+)")
REVIEW_RULINGS = {
    "grammar-bug",
    "c-parser-bug",
    "reviewed-reference-gap",
    "spec-gap",
}


@dataclass(frozen=True)
class ParserStatus:
    tree_status: str
    c_status: str
    result: LambdaProcessResult

    @property
    def differs(self) -> bool:
        return self.tree_status != self.c_status


@dataclass(frozen=True)
class ReviewEntry:
    case_id: str
    source_hash: str
    tree_status: str
    c_status: str
    ruling: str
    formal_refs: str
    fixture: Path
    issue: str


def _source_hash(source: Path) -> str:
    return hashlib.sha256(source.read_bytes()).hexdigest()


def load_review_manifest(manifest: Path) -> dict[Path, ReviewEntry]:
    if not manifest.is_file():
        raise ValueError(f"missing parser review manifest: {manifest}")
    entries: dict[Path, ReviewEntry] = {}
    for line_number, raw_line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 8:
            raise ValueError(f"{manifest}:{line_number}: expected 8 tab-separated fields")
        case_id, source_hash, tree_status, c_status, ruling, formal_refs, fixture_name, issue = fields
        fixture = (SCRIPT_DIR / fixture_name).resolve()
        if not fixture.is_file():
            raise ValueError(f"{manifest}:{line_number}: missing fixture {fixture_name}")
        if tree_status not in ("accept", "reject") or c_status not in ("accept", "reject"):
            raise ValueError(f"{manifest}:{line_number}: invalid acceptance status")
        if tree_status == c_status:
            raise ValueError(f"{manifest}:{line_number}: review row is not a disagreement")
        if ruling not in REVIEW_RULINGS:
            raise ValueError(f"{manifest}:{line_number}: invalid ruling {ruling}")
        if not formal_refs:
            raise ValueError(f"{manifest}:{line_number}: formal references are required")
        if fixture in entries:
            raise ValueError(f"{manifest}:{line_number}: duplicate fixture {fixture_name}")
        entries[fixture] = ReviewEntry(
            case_id, source_hash, tree_status, c_status, ruling, formal_refs, fixture, issue)
    return entries


def check_source(executable: Path, source: Path, work_dir: Path,
                 timeout: float) -> ParserStatus:
    """Runs the existing isolated `lambda-cst` verifier for one source."""
    work_dir.mkdir(parents=True, exist_ok=True)
    manifest = work_dir / f"parser-{_source_hash(source)[:20]}.tsv"
    # lambda-cst consumes the first tab-separated field as a source path.
    manifest.write_text(f"path\tbytes\tsha256\n{source}\t0\t0\n", encoding="utf-8")
    result = run_process([str(executable), str(manifest)], timeout)
    if result.launch_error or result.timed_out:
        return ParserStatus("error", "error", result)
    summary = SUMMARY_RE.search(result.stderr)
    if summary is None:
        return ParserStatus("error", "error", result)
    total, tree_ok, c_ok, _, _ = (int(value) for value in summary.groups())
    if total != 1:
        return ParserStatus("error", "error", result)
    return ParserStatus(
        "accept" if tree_ok else "reject",
        "accept" if c_ok else "reject",
        result,
    )


def check_stability(executable: Path, source: Path, work_dir: Path,
                    timeout: float) -> LambdaProcessResult:
    """Checks fail-fast/recovery determinism without requiring grammar agreement."""
    work_dir.mkdir(parents=True, exist_ok=True)
    manifest = work_dir / f"parser-stability-{_source_hash(source)[:20]}.tsv"
    manifest.write_text(f"path\tbytes\tsha256\n{source}\t0\t0\n", encoding="utf-8")
    result = run_process(
        [str(executable), "--lambda-stability", str(manifest)], timeout)
    if result.launch_error or result.timed_out:
        return result
    summary = STABILITY_RE.search(result.stderr)
    if summary is None or summary.group(1) != "1" or summary.group(2) != "0":
        return LambdaProcessResult(
            result.argv, result.stdout, result.stderr, 1, False,
            "parser stability verifier did not report one stable source")
    return result


def review_source(status: ParserStatus, source: Path,
                  reviews: dict[Path, ReviewEntry]) -> tuple[str, str]:
    """Classifies a difference and rejects stale, unreviewed, or unresolved rows."""
    if status.tree_status == "error" or status.c_status == "error":
        return "infrastructure-error", f"parser verifier did not report {source}"
    if not status.differs:
        if source in reviews:
            return "parser-review-stale", f"reviewed difference resolved: {source}"
        return "parser-agreement", f"{status.tree_status}/{status.c_status}"
    review = reviews.get(source)
    if review is None:
        return "parser-review", (
            f"unreviewed {status.tree_status}/{status.c_status}: {source}; "
            "adjudicate under D8.1.2v3")
    if review.source_hash != _source_hash(source):
        return "parser-review-stale", f"fixture content changed: {source}"
    if (review.tree_status, review.c_status) != (status.tree_status, status.c_status):
        return "parser-review-stale", f"fixture classification changed: {source}"
    if review.ruling != "reviewed-reference-gap":
        return "parser-review-unresolved", f"{review.case_id}: {review.ruling}"
    return "reviewed-parser-gap", f"{review.case_id}: {review.issue}"


def validate_reviews(executable: Path, reviews: dict[Path, ReviewEntry],
                     work_dir: Path, timeout: float) -> list[tuple[str, str, ParserStatus]]:
    """Rechecks every tracked exception before generated candidates are trusted."""
    results: list[tuple[str, str, ParserStatus]] = []
    for fixture in sorted(reviews):
        status = check_source(executable, fixture, work_dir, timeout)
        outcome, message = review_source(status, fixture, reviews)
        results.append((outcome, message, status))
    return results
