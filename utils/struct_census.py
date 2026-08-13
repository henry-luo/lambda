#!/usr/bin/env python3
"""Clang-based data-structure census for the Lambda codebase.

Parses every translation unit with libclang and records each struct/class/
union/enum *definition* declared under the configured source dirs. Unlike a
regex scan this sees through macros, typedefs and the include closure, and it
reports the compiler's real record layout (size/alignment), not just the
declaration site.

Outputs land in the configured output dir (default ``vibe/meta/ds``):
  <stem>.json  canonical metadata + the incremental cache
  <stem>.csv   flat table, one row per record, module-tagged

The interactive viewer over this data lives in the DevTool app (`make devtool`,
left panel -> Struct Census), which reads the CSV and can re-run this script.

Runs are incremental: each TU stores a signature over itself plus every
repo-local header it pulled in, so a re-run only re-parses TUs whose
dependency set actually changed. See utils/Struct_Census.md.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import sys
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "utils" / "struct_census.config.json"
STATE_VERSION = 1

# cursor kind name -> the kind label we report
KIND_NAMES = {
    "STRUCT_DECL": "struct",
    "CLASS_DECL": "class",
    "UNION_DECL": "union",
    "ENUM_DECL": "enum",
}

CFG: dict = {}      # per-worker config, installed by _worker_init
_CINDEX = None      # per-worker clang.cindex module handle
_RESOURCE_DIR = ""  # clang builtin-header dir matching the loaded libclang


# ---------------------------------------------------------------- libclang

def load_clang(cfg: dict):
    """Import clang.cindex and bind it to a libclang shared library.

    Homebrew/apt LLVM ship the Python bindings alongside the dylib, so prefer a
    matched pair from `libclang_search` over whatever happens to be on
    sys.path — a bindings/library version skew fails at the first parse.
    """
    global _CINDEX, _RESOURCE_DIR
    if _CINDEX is not None:
        return _CINDEX

    for base in cfg["clang"].get("libclang_search", []):
        b = Path(base)
        libs = sorted(b.glob("lib/libclang.dylib")) + sorted(b.glob("lib/libclang.so*"))
        sites = sorted(b.glob("lib/python3.*/site-packages"))
        if not libs or not sites:
            continue
        sys.path.insert(0, str(sites[-1]))
        import clang.cindex as ci  # noqa: PLC0415
        ci.Config.set_library_file(str(libs[0]))
        # libclang is driven through the C API, so nothing computes the builtin
        # header path the way the clang driver does from argv[0]: without an
        # explicit -resource-dir every TU fails on <stdarg.h>/<stdatomic.h>.
        res = sorted(b.glob("lib/clang/*"))
        if res:
            _RESOURCE_DIR = str(res[-1])
        _CINDEX = ci
        return ci

    try:
        import clang.cindex as ci  # noqa: PLC0415
    except ImportError:
        raise SystemExit(
            "struct-census: no libclang bindings found.\n"
            "  install LLVM (brew install llvm / apt install libclang-dev python3-clang)\n"
            "  or add its prefix to clang.libclang_search in the config file."
        )
    _CINDEX = ci
    return ci


# ------------------------------------------------------------ path helpers

def is_excluded(rel: str, cfg: dict) -> bool:
    """Exclude entries are plain path prefixes.

    No implicit directory boundary: 'lambda/tree-sitter' is meant to cover every
    tree-sitter-* sibling. Write a trailing '/' to pin an entry to one directory.
    """
    if any(rel.startswith(pre) for pre in cfg["exclude_dirs"]):
        return True
    return rel in cfg["exclude_files"]


def owned(path: str, cfg: dict) -> str | None:
    """Repo-relative path if this file is ours to audit, else None."""
    try:
        rel = os.path.relpath(path, ROOT)
    except ValueError:
        return None
    if rel.startswith(".."):
        return None
    if not any(rel == d or rel.startswith(d + "/") for d in cfg["source_dirs"]):
        return None
    return None if is_excluded(rel, cfg) else rel


def module_of(rel: str, cfg: dict) -> str:
    """Map a file to a static module by longest matching path prefix.

    Prefixes are plain, so a rule can name a directory ('lambda/js') or a file
    stem ('lambda/main' covers main.cpp and main-repl.cpp). Anything unmatched
    lands in the '*' fallback, which the config sets to 'unmapped' so a new
    source dir shows up in the report instead of silently joining a module.
    """
    rules = cfg["module_rules"]
    mod, best = rules.get("*", "unmapped"), -1
    for pre, m in rules.items():
        if pre != "*" and rel.startswith(pre) and len(pre) > best:
            best, mod = len(pre), m
    return mod


def discover_tus(cfg: dict) -> list[str]:
    exts = tuple(cfg["tu_extensions"])
    out = []
    for src in cfg["source_dirs"]:
        for dirpath, dirnames, filenames in os.walk(ROOT / src):
            rel_dir = os.path.relpath(dirpath, ROOT)
            # prune excluded subtrees so we never walk into sqlite/tree-sitter
            dirnames[:] = [
                d for d in dirnames
                if not is_excluded(os.path.join(rel_dir, d).replace("./", ""), cfg)
            ]
            for fn in filenames:
                if not fn.endswith(exts):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, fn), ROOT)
                if not is_excluded(rel, cfg):
                    out.append(rel)
    return sorted(out)


# ------------------------------------------------------------- signatures

def stat_token(rel: str) -> str:
    try:
        st = os.stat(rel)
        return f"{rel}:{st.st_size}:{st.st_mtime_ns}"
    except OSError:
        return f"{rel}:missing"


def dep_sig(deps: list[str]) -> str:
    h = hashlib.sha1()
    for d in sorted(deps):
        h.update(stat_token(d).encode())
        h.update(b"\n")
    return h.hexdigest()


def config_sig(cfg: dict) -> str:
    """Cache key for everything that is not a source file.

    Hashes this script alongside the config: an edit to the extraction logic
    changes what a cached record means, so it has to invalidate the cache too.
    """
    h = hashlib.sha1(json.dumps(cfg, sort_keys=True).encode())
    h.update(Path(__file__).read_bytes())
    return h.hexdigest()


# ------------------------------------------------------------------ parse

def _worker_init(cfg: dict) -> None:
    global CFG
    CFG = cfg
    os.chdir(ROOT)          # include dirs in the config are repo-relative
    load_clang(cfg)


def tu_args(rel: str, cfg: dict) -> list[str]:
    c = cfg["clang"]
    is_c = rel.endswith(".c")
    if rel in cfg.get("objcxx_files", []):
        args = ["-x", "objective-c++", f"-std={c['cxx_std']}"]
    elif is_c:
        args = ["-x", "c", f"-std={c['c_std']}"] + c.get("c_flags", [])
    else:
        args = ["-x", "c++", f"-std={c['cxx_std']}"]
    args += [f"-D{d}" for d in c.get("defines", [])]
    args += [f"-I{i}" for i in c["include_dirs"]]
    if _RESOURCE_DIR:
        args += ["-resource-dir", _RESOURCE_DIR]
    sdk = os.environ.get("SDKROOT")
    if sdk:
        args.append(f"-isysroot{sdk}")
    return args


def scan(rel: str) -> dict:
    """Parse one TU; return its records, repo-local deps, and error count."""
    ci = load_clang(CFG)
    kinds = set(CFG["kinds"])
    index = ci.Index.create()
    try:
        tu = index.parse(
            rel, args=tu_args(rel, CFG),
            options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES
                    | ci.TranslationUnit.PARSE_INCOMPLETE,
        )
    except ci.TranslationUnitLoadError:
        return {"tu": rel, "records": [], "deps": [rel], "fatal": True, "errors": 0}

    deps = {rel}
    for inc in tu.get_includes():
        o = owned(inc.include.name, CFG)
        if o:
            deps.add(o)

    records, seen = [], set()
    for c in tu.cursor.walk_preorder():
        kind = KIND_NAMES.get(c.kind.name)
        if kind is None or kind not in kinds or not c.is_definition():
            continue
        loc = c.location
        if loc.file is None:
            continue
        f = owned(loc.file.name, CFG)
        if f is None:
            continue
        usr = c.get_usr()
        if usr in seen:
            continue
        seen.add(usr)

        if kind == "enum":
            members = sum(1 for ch in c.get_children()
                          if ch.kind == ci.CursorKind.ENUM_CONSTANT_DECL)
        else:
            members = sum(1 for _ in c.type.get_fields())
        size, align = c.type.get_size(), c.type.get_align()
        # a typedef'd anonymous record still gets its typedef name from
        # type.spelling; a truly anonymous one gets clang's "(unnamed … at
        # path:line)" placeholder (bare in C++, tag-prefixed in C), which just
        # duplicates the file/line columns
        name = c.spelling or c.type.spelling
        if not name or "(unnamed" in name or "(anonymous" in name:
            name = "(anonymous)"
        records.append({
            "usr": usr,
            "name": name,
            "kind": kind,
            "module": module_of(f, CFG),
            "file": f,
            "line": loc.line,
            "size": size if size > 0 else None,
            "align": align if align > 0 else None,
            "members": members,
        })

    errors = sum(1 for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error)
    return {"tu": rel, "records": records, "deps": sorted(deps),
            "fatal": False, "errors": errors}


# ----------------------------------------------------------------- state

def load_state(path: Path, cfg: dict, full: bool) -> dict:
    """Return {tu: {"sig", "deps", "records"}} reusable from the last run."""
    if full or not path.exists():
        return {}
    try:
        st = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return {}
    # a config change can alter kinds, excludes or flags: everything is suspect
    if st.get("version") != STATE_VERSION or st.get("config_sig") != config_sig(cfg):
        return {}
    files, recs = st.get("files", []), st.get("records", [])
    cache = {}
    for tu, e in st.get("tus", {}).items():
        try:
            cache[tu] = {
                "sig": e["sig"],
                "deps": [files[i] for i in e["deps"]],
                "records": [recs[i] for i in e["recs"]],
            }
        except (IndexError, KeyError):
            return {}
    return cache


# --------------------------------------------------------------- reports

def write_json(path: Path, records: list[dict], per_tu: dict, cfg: dict,
               generated: str, totals: dict) -> None:
    idx = {r["usr"]: i for i, r in enumerate(records)}
    files: dict[str, int] = {}
    tus = {}
    for tu in sorted(per_tu):
        e = per_tu[tu]
        tus[tu] = {
            "sig": e["sig"],
            "deps": [files.setdefault(d, len(files)) for d in e["deps"]],
            "recs": sorted(idx[r["usr"]] for r in e["records"] if r["usr"] in idx),
        }
    path.write_text(json.dumps({
        "version": STATE_VERSION,
        "generated": generated,
        "config_sig": config_sig(cfg),
        "source_dirs": cfg["source_dirs"],
        "totals": totals,
        "records": records,
        "files": list(files),
        "tus": tus,
    }, indent=1))


CSV_COLUMNS = ["module", "kind", "name", "size", "align", "members", "file", "line"]


def write_csv(path: Path, records: list[dict]) -> None:
    with path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(CSV_COLUMNS)
        for r in records:
            w.writerow([r[c] if r[c] is not None else "" for c in CSV_COLUMNS])


# ------------------------------------------------------------------ main

def main() -> int:
    ap = argparse.ArgumentParser(description="Lambda data-structure census")
    ap.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    ap.add_argument("--full", action="store_true",
                    help="ignore the cache and re-parse every translation unit")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--top", type=int, default=15, help="rows in the stdout summary")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    cfg = json.loads(args.config.read_text())
    os.chdir(ROOT)
    if sys.platform == "darwin" and not os.environ.get("SDKROOT"):
        sdk = os.popen("xcrun --show-sdk-path").read().strip()
        if sdk:
            os.environ["SDKROOT"] = sdk

    out_dir = ROOT / cfg["output_dir"]
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = cfg["output_stem"]
    json_path = out_dir / f"{stem}.json"

    tus = discover_tus(cfg)
    cache = load_state(json_path, cfg, args.full)

    stale = [t for t in tus if t not in cache or cache[t]["sig"] != dep_sig(cache[t]["deps"])]
    fresh = {t: cache[t] for t in tus if t not in stale}

    if not args.quiet:
        mode = "full" if args.full or not cache else "incremental"
        print(f"struct-census: {len(tus)} TUs ({mode}); "
              f"{len(fresh)} cached, {len(stale)} to parse")

    per_tu = dict(fresh)
    fatal, errored = [], []
    if stale:
        with ProcessPoolExecutor(max_workers=args.j, initializer=_worker_init,
                                 initargs=(cfg,)) as pool:
            for i, res in enumerate(pool.map(scan, stale), 1):
                per_tu[res["tu"]] = {
                    "sig": dep_sig(res["deps"]),
                    "deps": res["deps"],
                    "records": res["records"],
                }
                if res["fatal"]:
                    fatal.append(res["tu"])
                elif res["errors"]:
                    errored.append((res["tu"], res["errors"]))
                if not args.quiet and sys.stderr.isatty():
                    print(f"\r  parsed {i}/{len(stale)}", end="", file=sys.stderr)
        if not args.quiet and sys.stderr.isatty():
            print(file=sys.stderr)

    # dedup across TUs by USR; sort for a stable, diffable record order
    by_usr: dict[str, dict] = {}
    for tu in sorted(per_tu):
        for r in per_tu[tu]["records"]:
            by_usr.setdefault(r["usr"], r)
    records = sorted(by_usr.values(), key=lambda r: (r["module"], r["file"], r["line"], r["name"]))

    by_kind: dict[str, int] = defaultdict(int)
    by_module: dict[str, int] = defaultdict(int)
    for r in records:
        by_kind[r["kind"]] += 1
        by_module[r["module"]] += 1
    totals = {
        "records": len(records),
        "tus": len(tus),
        "by_kind": dict(sorted(by_kind.items())),
        "by_module": dict(sorted(by_module.items(), key=lambda kv: -kv[1])),
    }

    generated = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    write_json(json_path, records, per_tu, cfg, generated, totals)
    write_csv(out_dir / f"{stem}.csv", records)

    if args.quiet:
        return 0

    kinds = ", ".join(f"{n} {k}" for k, n in totals["by_kind"].items())
    print(f"\n{len(records)} definitions ({kinds}) in {len(by_module)} modules\n")
    print(f"top {args.top} modules:")
    for m, n in list(totals["by_module"].items())[:args.top]:
        print(f"  {n:5d}  {m}")
    sized = [r for r in records if r["size"]]
    print(f"\ntop {args.top} by size ({len(sized)} laid out):")
    for r in sorted(sized, key=lambda r: -r["size"])[:args.top]:
        print(f"  {r['size']:9,d} B  {r['members']:4d} members  {r['name']:<32} "
              f"{r['file']}:{r['line']}")
    if fatal:
        print(f"\n{len(fatal)} TU(s) failed to parse at all: {', '.join(fatal[:5])}"
              f"{' …' if len(fatal) > 5 else ''}", file=sys.stderr)
    if errored:
        worst = sorted(errored, key=lambda e: -e[1])[:3]
        print(f"{len(errored)} TU(s) parsed with errors (their records may be "
              f"incomplete): " + ", ".join(f"{t} ({n})" for t, n in worst),
              file=sys.stderr)
    print(f"\nwrote {cfg['output_dir']}/{stem}.{{json,csv}}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
