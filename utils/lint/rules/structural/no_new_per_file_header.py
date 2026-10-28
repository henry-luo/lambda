#!/usr/bin/env python3
"""Enforce the Radiant global-header allow-list and header-count ratchet."""
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
    # These are the public Radiant network-provider seams introduced by the
    # static-module split, not private source-file mirrors.
    "network_integration.h",
    "resource_loaders.h",
}

HEADER_COUNT_MAX = 24
SOURCE_EXTS = (".c", ".cpp", ".mm")
HEADER_EXTS = (".h", ".hpp")

def repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def git_status_paths(root: Path) -> dict[str, str]:
    proc = subprocess.run(
        ["git", "status", "--porcelain", "--untracked-files=all", "--", "radiant"],
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


def relocated_headers(root: Path, headers: list[Path], changed: dict[str, str]) -> set[str]:
    """Keep an unambiguous, same-name tracked header move on its existing debt."""
    proc = subprocess.run(
        ["git", "ls-files", "--deleted", "--", "radiant"],
        cwd=root, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, check=False,
    )
    removed: dict[str, list[str]] = {}
    added: dict[str, list[str]] = {}
    for name in proc.stdout.splitlines():
        path = Path(name)
        if path.suffix in HEADER_EXTS:
            removed.setdefault(path.name, []).append(name)
    for header in headers:
        rel = header.relative_to(root).as_posix()
        status = changed.get(rel, "")
        if status.startswith("A") or status.startswith("??"):
            added.setdefault(header.name, []).append(rel)
    # Header count and existing violations still apply; ambiguous moves stay new.
    return {paths[0] for name, paths in added.items()
            if len(paths) == 1 and len(removed.get(name, [])) == 1}


def main() -> int:
    root = repo_root()
    changed = git_status_paths(root)
    headers = radiant_headers(root)
    relocated = relocated_headers(root, headers, changed)

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
        is_new_header = (status.startswith("A") or status.startswith("??")) and repo_rel not in relocated
        if is_new_header and (not is_allowed or (has_sibling and not is_allowed)):
            new_violations.append((rel, is_allowed, has_sibling))
    over_cap = len(headers) > HEADER_COUNT_MAX
    mode = os.environ.get("RADIANT_HEADER_LINT_MODE", "error")
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
    print(f"{prefix}: Radiant header count {len(headers)}/{HEADER_COUNT_MAX}")
    if over_cap:
        print(f"{prefix}: header count exceeds ratchet cap")
    if disallowed or per_file:
        print(
            f"{prefix}: violations: "
            f"{len(disallowed)} non-allow-listed, {len(per_file)} per-file mirror(s)"
        )

    # This is a ratchet, not a retroactive repository-wide header migration.
    # Existing tracked exceptions remain visible above, while only a newly
    # introduced violation (or growth beyond the global cap) fails CI.
    if mode == "error" and (new_violations or over_cap):
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
