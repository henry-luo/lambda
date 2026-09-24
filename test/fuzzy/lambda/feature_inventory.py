"""Inventory ratchet for Lambda fuzz-support surfaces (D1.10).

This intentionally snapshots production declarations instead of a hand-kept
token list. A new token, parser reduction, AST node, or registry row changes
the lock hash and fails the smoke gate until its campaign status is reviewed.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re


SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parents[2]
LOCK_PATH = SCRIPT_DIR / "feature_inventory.lock"
CAMPAIGN_PATH = SCRIPT_DIR / "feature_campaigns.tsv"
SEMANTIC_CAMPAIGN_PATH = SCRIPT_DIR / "semantic_campaigns.tsv"
SEMANTIC_CAMPAIGNS = {
    "syntax-parser",
    "values-types-representation",
    "operators-equality-order",
    "errors-functions-resources",
    "mutation-lifetime",
    "concurrency",
    "data-processing",
    "modules",
}


def strip_c_comments(source: str) -> str:
    # one alternation, so whichever comment opens first wins, as in C
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.DOTALL)


def enum_values(path: Path, declaration: str, prefix: str) -> list[str]:
    source = path.read_text(encoding="utf-8")
    start = source.find(declaration)
    if start < 0:
        raise ValueError(f"missing declaration {declaration} in {path}")
    # Comments are not declarations: a `}` inside one (`open t = doc { ... }`
    # in LambdaReductionForm) cut the block short, and a name inside one
    # (`AST_FOR` in AstNodeType) counted as a value.
    block = strip_c_comments(source[start:])
    end = block.find("}")
    if end < 0:
        raise ValueError(f"unterminated declaration {declaration} in {path}")
    return sorted(set(re.findall(rf"\b({re.escape(prefix)}[A-Z0-9_]+)\b", block[:end])))


def system_function_rows(path: Path) -> list[str]:
    source = path.read_text(encoding="utf-8")
    rows = re.findall(r'\{(SYSFUNC_[A-Z0-9_]+),\s*"([^"]+)",\s*(-?[0-9]+)', source)
    if not rows:
        raise ValueError(f"no SysFuncInfo rows found in {path}")
    return sorted({f"{identifier}:{name}/{arity}" for identifier, name, arity in rows})


def inventory() -> list[str]:
    parser_header = PROJECT_ROOT / "lambda/runtime/parser/lambda_rd_parser.h"
    ast_header = PROJECT_ROOT / "lambda/runtime/ast-core.hpp"
    registry = PROJECT_ROOT / "lambda/runtime/sys_func_registry.c"
    rows: list[str] = []
    rows.extend(f"token\t{value}" for value in enum_values(
        parser_header, "typedef enum LambdaTokenKind", "LAMBDA_TOK_"))
    rows.extend(f"reduction-kind\t{value}" for value in enum_values(
        parser_header, "typedef enum LambdaReductionKind", "LAMBDA_REDUCE_"))
    rows.extend(f"reduction-form\t{value}" for value in enum_values(
        parser_header, "typedef enum LambdaReductionForm", "LAMBDA_REDUCTION_FORM_"))
    rows.extend(f"ast\t{value}" for value in enum_values(
        ast_header, "typedef enum AstNodeType", "AST_"))
    rows.extend(f"sysfunc\t{value}" for value in system_function_rows(registry))
    return sorted(rows)


def inventory_digest(rows: list[str]) -> str:
    return hashlib.sha256(("\n".join(rows) + "\n").encode("utf-8")).hexdigest()


def campaign_statuses(path: Path) -> dict[str, tuple[str, str, str]]:
    if not path.is_file():
        raise ValueError(f"missing feature campaign map: {path}")
    statuses: dict[str, tuple[str, str, str]] = {}
    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            raise ValueError(f"{path}:{line_number}: expected 4 tab-separated fields")
        surface, status, evidence, refs = fields
        if status not in ("covered", "unsupported-with-reason"):
            raise ValueError(f"{path}:{line_number}: invalid status {status}")
        if not evidence or not refs:
            raise ValueError(f"{path}:{line_number}: evidence and references are required")
        if surface in statuses:
            raise ValueError(f"{path}:{line_number}: duplicate surface {surface}")
        statuses[surface] = (status, evidence, refs)
    return statuses


def expected_lock(rows: list[str]) -> str:
    return f"version\t1\ncount\t{len(rows)}\nsha256\t{inventory_digest(rows)}\n"


def check_inventory(lock_path: Path = LOCK_PATH,
                    campaign_path: Path = CAMPAIGN_PATH) -> str:
    rows = inventory()
    surfaces = {row.split("\t", 1)[0] for row in rows}
    statuses = campaign_statuses(campaign_path)
    missing = sorted(surfaces - statuses.keys())
    stale = sorted(statuses.keys() - surfaces)
    if missing or stale:
        raise ValueError(f"feature campaign map mismatch: missing={missing} stale={stale}")
    if not lock_path.is_file():
        raise ValueError(f"missing feature inventory lock: {lock_path}")
    expected = expected_lock(rows)
    actual = lock_path.read_text(encoding="utf-8")
    if actual != expected:
        raise ValueError(
            "feature inventory changed; review campaign coverage and update "
            "test/fuzzy/lambda/feature_inventory.lock")
    semantic_campaigns = campaign_statuses(SEMANTIC_CAMPAIGN_PATH)
    semantic_missing = sorted(SEMANTIC_CAMPAIGNS - semantic_campaigns.keys())
    semantic_stale = sorted(semantic_campaigns.keys() - SEMANTIC_CAMPAIGNS)
    if semantic_missing or semantic_stale:
        raise ValueError(
            f"semantic campaign map mismatch: missing={semantic_missing} stale={semantic_stale}")
    summary = ", ".join(
        f"{surface}={sum(1 for row in rows if row.startswith(surface + chr(9)))}"
        for surface in sorted(surfaces))
    return f"feature inventory verified: {summary}; campaigns={len(semantic_campaigns)}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Check Lambda fuzz feature inventory")
    parser.add_argument("--print-lock", action="store_true",
                        help="print the reviewed lock content without writing files")
    parser.add_argument("--list", action="store_true", help="print the canonical inventory")
    args = parser.parse_args()
    rows = inventory()
    if args.print_lock:
        print(expected_lock(rows), end="")
        return 0
    if args.list:
        print("\n".join(rows))
        return 0
    try:
        print(check_inventory())
    except ValueError as error:
        print(error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
