#!/usr/bin/env python3
"""Warn about Radiant headers that violate the DD4 global-header plan."""
import os
import subprocess
import sys
from pathlib import Path

ALLOW_LIST = {
    "view.hpp",
    "layout.hpp",
    "render.hpp",
    "event.hpp",
    "radiant.hpp",
    "webdriver/webdriver.hpp",
    "webview_handle_mac.h",
    "webview_handle_linux.h",
    "rdt_video.h",
    "grid_sizing_algorithm.hpp",
    "grid_enhanced_adapter.hpp",
    "grid_placement.hpp",
    "grid_track.hpp",
    "grid_occupancy.hpp",
    "glyph_sampling.hpp",
    "render_effect_raster_fallback.hpp",
    "render_glyph_run_raster_lower.hpp",
    "state_store_internal.hpp",
    "event_sim.hpp",
    "graph_layout_types.hpp",
    "graph_theme.hpp",
    "graph_to_svg.hpp",
    "layout_graph.hpp",
}

SOURCE_EXTS = (".c", ".cpp", ".mm")
HEADER_EXTS = (".h", ".hpp")

def repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def git_status_paths(root: Path) -> dict[str, str]:
    proc = subprocess.run(
        ["git", "status", "--porcelain", "--", "radiant"],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    paths: dict[str, str] = {}
    for line in proc.stdout.splitlines():
        if len(line) < 4: continue
        status = line[:2]
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        paths[path] = status
    return paths


def sibling_source_exists(header: Path) -> bool:
    base = header.with_suffix("")
    return any(base.with_suffix(ext).exists() for ext in SOURCE_EXTS)

def radiant_headers(root: Path) -> list[Path]:
    radiant = root / "radiant"
    return sorted(
        path for path in radiant.rglob("*")
        if path.is_file() and path.suffix in HEADER_EXTS
    )

def rel_radiant(root: Path, path: Path) -> str:
    return path.relative_to(root / "radiant").as_posix()

def main() -> int:
    root = repo_root()
    changed = git_status_paths(root)
    headers = radiant_headers(root)

    disallowed = []
    per_file = []
    new_violations = []
    for header in headers:
        rel = rel_radiant(root, header)
        repo_rel = f"radiant/{rel}"
        is_allowed = rel in ALLOW_LIST
        has_sibling = sibling_source_exists(header)

        if not is_allowed:
            disallowed.append(rel)
        if has_sibling and not is_allowed:
            per_file.append(rel)
        status = changed.get(repo_rel, "")
        is_new_header = status.startswith("A") or status.startswith("??")
        if is_new_header and (not is_allowed or (has_sibling and not is_allowed)):
            new_violations.append((rel, is_allowed, has_sibling))
    mode = os.environ.get("RADIANT_HEADER_LINT_MODE", "warn")
    prefix = "no-new-per-file-header"
    if new_violations:
        print(f"{prefix}: {len(new_violations)} new Radiant header(s) violate DD4:")
        for rel, is_allowed, has_sibling in new_violations:
            reasons = []
            if not is_allowed:
                reasons.append("not in global-header allow-list")
            if has_sibling:
                reasons.append("mirrors a sibling source file")
            print(f"  radiant/{rel}: {', '.join(reasons)}")
    else:
        print(f"{prefix}: no new Radiant headers violate DD4")
    if disallowed or per_file:
        # Legacy headers remain during H1-H5; keep default output concise until
        # H6 flips the ratchet to error mode.
        print(
            f"{prefix}: legacy inventory: "
            f"{len(disallowed)} non-allow-listed, {len(per_file)} per-file mirror(s)"
        )

    if mode == "error" and (new_violations or disallowed or per_file):
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
