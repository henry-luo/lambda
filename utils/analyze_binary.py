#!/usr/bin/env python3
"""
Binary Size Analysis for Lambda/Radiant executables.

Produces a Markdown table showing each library group's contribution to the
final binary, along with symbol counts and notes.

Methodology
-----------
1. Parse build_lambda_config.json to discover all static libraries.
2. For each .a, extract *defined external symbols* via `nm -g`.
3. Parse `nm -nm` of the binary to get each symbol's address AND section.
4. Parse `otool -l` to get section address ranges.
5. Compute symbol sizes via consecutive-address, CAPPED at the current
   section's end — eliminating the notorious cross-section overcount.
6. Match library .a symbols → binary symbols; sum sizes per group.
7. Symbols not claimed by any library get prefix-classified or go to
   "Project code + LTO-hidden".

Usage
-----
    python3 utils/analyze_binary.py [binary] [--config path]

    binary   defaults to lambda.exe
    --config defaults to build_lambda_config.json

Caveats
-------
• LTO + `-Wl,-x` internalize symbols for some libs (curl, re2, ThorVG).
  These are flagged as "LTO-invisible".  Sizes from prior stub-link
  experiments are used where available.
• The consecutive-address method measures the gap between two adjacent
  *visible* symbols.  With heavy LTO, many internal symbols are stripped,
  so gaps may contain code from multiple libraries.  The section-boundary
  cap prevents the worst overcounts, but sizes remain estimates.
• woff2 is compiled from source (not a .a).  Its symbols are detected
  by prefix matching.
"""

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from collections import defaultdict

# ── Configurable constants ────────────────────────────────────────────
DEFAULT_BINARY = "lambda.exe"
DEFAULT_CONFIG = "build_lambda_config.json"

# ── Library grouping rules ────────────────────────────────────────────
# Maps a library name (from build config) to a display group.
# Libraries sharing a group name are merged in the report.
LIBRARY_GROUPS = {
    # tree-sitter family
    "tree-sitter":             "tree-sitter",
    "tree-sitter-lambda":      "tree-sitter",
    "tree-sitter-javascript":  "tree-sitter",
    "tree-sitter-latex":       "tree-sitter",
    "tree-sitter-latex-math":  "tree-sitter",
    # mbedTLS family
    "mbedtls":                 "mbedTLS",
    "mbedx509":                "mbedTLS",
    "mbedcrypto":              "mbedTLS",
    # brotli family
    "brotlidec":               "brotli",
    "brotlicommon":            "brotli",
    # libevent family
    "libevent":                "libevent",
    "libevent_openssl":        "libevent",
    # image family — keep separate
    "turbojpeg":               "turbojpeg",
    "png":                     "libpng",
    "gif":                     "libgif",
    # everything else maps 1:1
}

# C++ runtime symbols that appear in many .a files but belong to
# the system C++ runtime — exclude from library attribution.
CPP_RUNTIME_SYMBOLS = {
    "__ZdlPv",        # operator delete(void*)
    "__Znwm",         # operator new(unsigned long)
    "__ZdaPv",        # operator delete[](void*)
    "__Znam",         # operator new[](unsigned long)
    "__ZSt9terminatev",
    "___cxa_pure_virtual",
    "___gxx_personality_v0",
}

# Symbol-prefix → group for unclaimed symbols (fallback heuristic).
# Checked in order; first match wins.
SYMBOL_PREFIX_GROUPS = [
    ("_ts_",            "tree-sitter"),
    ("_tree_sitter_",   "tree-sitter"),
    ("_mbedtls_",       "mbedTLS"),
    ("_psa_",           "mbedTLS"),
    ("_MIR_",           "MIR"),
    ("_HTAB_",          "MIR"),
    ("_VARR_",          "MIR"),
    ("_DLIST_",         "MIR"),
    ("_BITMAP_",        "MIR"),
    ("_c2mir",          "MIR"),
    ("_va_block_arg_builtin", "MIR"),
    ("_va_arg_builtin", "MIR"),
    ("_mpd_",           "mpdec"),
    ("_jpeg_",          "turbojpeg"),
    ("_jpeg12_",        "turbojpeg"),
    ("_jsimd_",         "turbojpeg"),
    ("_j12",            "turbojpeg"),
    ("_j16",            "turbojpeg"),
    ("_jzero_",         "turbojpeg"),
    ("_jround_",        "turbojpeg"),
    ("_tj",             "turbojpeg"),
    ("_jinit_",         "turbojpeg"),
    ("_jcopy_",         "turbojpeg"),
    ("_png_",           "libpng"),
    ("_DGif",           "libgif"),
    ("_EGif",           "libgif"),
    ("_Gif",            "libgif"),
    ("_glfw",           "GLFW"),
    ("__glfw",          "GLFW"),
    ("_event_",         "libevent"),
    ("_evbuffer_",      "libevent"),
    ("_evutil_",        "libevent"),
    ("_evhttp_",        "libevent"),
    ("_bufferevent_",   "libevent"),
    ("_evthread_",      "libevent"),
    ("_evdns_",         "libevent"),
    ("_evconnlistener_","libevent"),
    ("_listener_",      "libevent"),
    ("_selectops",      "libevent"),
    ("_pollops",        "libevent"),
    ("_kqops",          "libevent"),
    ("_openbsd_reallocarray", "libevent"),
    ("_BZ2_",           "bzip2"),
    ("_FT_",            "freetype"),
    ("_ft_",            "freetype"),
    ("_TT_",            "freetype"),
    ("_BrotliDecoder",  "brotli"),
    ("_BrotliGetDictionary", "brotli"),
    ("__kBrotli",       "brotli"),
    ("_Woff2",          "woff2"),
    ("_woff2",          "woff2"),
    ("_rpmalloc",       "rpmalloc"),
    ("_rpfree",         "rpmalloc"),
    ("_rpcalloc",       "rpmalloc"),
    ("_rprealloc",      "rpmalloc"),
    ("_rpmemalign",     "rpmalloc"),
    ("_utf8proc_",      "utf8proc"),
    ("_nghttp2_",       "nghttp2"),
    ("_tvg_",           "ThorVG"),
    ("_inflate",        "zlib"),
    ("_deflate",        "zlib"),
    ("_crc32",          "zlib"),
    ("_adler32",        "zlib"),
    ("_compress",       "zlib"),
    ("_uncompress",     "zlib"),
    ("_zlibVersion",    "zlib"),
    ("_zError",         "zlib"),
    ("_z_errmsg",       "zlib"),
    ("_curl_",          "curl"),
]

# Known LTO-invisible libraries with sizes from stub-link experiments.
LTO_INVISIBLE_OVERRIDES = {
    "curl":   {"size": 680 * 1024, "note": "LTO-invisible; verified by stub-link"},
    "re2":    {"size": 214 * 1024, "note": "LTO-invisible; verified by stub-link"},
    "ThorVG": {"size": 0,         "note": "LTO-invisible; symbols internalized"},
    "woff2":  {"size": 205 * 1024, "note": "compiled from source; LTO-invisible"},
}

# Known stub-link verified sizes that override nm measurement.
# nm + consecutive-address is unreliable when LTO hides internal
# symbols, creating huge gaps attributed to the last visible symbol.
# These values were measured by building with stub .a files and
# comparing the binary size delta.  Only uncomment entries you've
# actually verified via stub-linking.
STUB_LINK_OVERRIDES = {
    # "library_group": size_in_bytes
    # Uncomment these once verified via stub-linking:
    # "MIR":         396 * 1024,    # c2mir: 180 KB + mir.o+mir-gen.o: 215 KB
    # "turbojpeg":   240 * 1024,    # with arith coding
    # "freetype":     87 * 1024,    # font rendering core
}


# ── Helper: run an external tool ──────────────────────────────────────

def run_tool(cmd, timeout=60):
    """Run a command and return stdout lines, or [] on failure."""
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout.splitlines()
    except Exception as e:
        print(f"Warning: {' '.join(cmd)} failed: {e}", file=sys.stderr)
        return []


# ── Parse sections from otool -l ──────────────────────────────────────

def get_sections(binary_path):
    """
    Parse `otool -l` to build a list of (start, end, segname, sectname).
    """
    lines = run_tool(["otool", "-l", binary_path], timeout=30)
    if not lines:
        print("Warning: otool -l returned no output.", file=sys.stderr)
        return []
    sections = []
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("sectname "):
            sect_name = stripped.split()[-1]
            seg_name = ""
            addr = None
            size = None
            # look ahead for segname, addr, size within this Section block
            for j in range(i + 1, min(i + 12, len(lines))):
                s = lines[j].strip()
                if s.startswith("segname "):
                    seg_name = s.split()[-1]
                elif s.startswith("addr "):
                    addr = int(s.split()[-1], 16)
                elif s.startswith("size "):
                    size = int(s.split()[-1], 16)
                elif s.startswith("Section") or s.startswith("Load command"):
                    break  # hit next section or load command — stop
                if seg_name and addr is not None and size is not None:
                    break
            if seg_name and addr is not None and size is not None and size > 0:
                sections.append((addr, addr + size, seg_name, sect_name))
        i += 1
    sections.sort()
    return sections


def find_section_end(addr, sections):
    """Return the end address of the section containing `addr`."""
    for s_start, s_end, _, _ in sections:
        if s_start <= addr < s_end:
            return s_end
    return None


def get_symbol_section_key(addr, sections):
    """Return (segname, sectname) for the section containing addr."""
    for s_start, s_end, seg, sect in sections:
        if s_start <= addr < s_end:
            return (seg, sect)
    return None


# ── Build symbol map from nm -nm ──────────────────────────────────────

def get_binary_symbols(binary_path):
    """
    Parse `nm -nm` (Mach-O verbose) to get symbols with section info.
    Also parse `nm -n` for sorted addresses.

    Returns:
        sorted_addrs: [(addr, name), ...] sorted by address
        sym_info:     {name: {"addr": int, "section": str, "type": str}}
    """
    # nm -nm gives: "0x... (segname,sectname) external _name"
    lines = run_tool(["nm", "-nm", binary_path])
    sym_info = {}
    for line in lines:
        # Defined symbols: "00000001000017fc (__TEXT,__text) external _ts_language_copy"
        m = re.match(
            r'^([0-9a-f]+)\s+\(([^)]+)\)\s+(?:external\s+)?(\S+)', line
        )
        if m:
            addr = int(m.group(1), 16)
            section = m.group(2)
            name = m.group(3)
            sym_info[name] = {"addr": addr, "section": section, "type": "T"}

    # nm -n for sorted addresses (includes S, D, B, C symbols too)
    lines2 = run_tool(["nm", "-n", binary_path])
    sorted_addrs = []
    for line in lines2:
        m = re.match(r'^([0-9a-f]+)\s+([A-Za-z])\s+(\S+)', line)
        if m:
            addr = int(m.group(1), 16)
            sym_type = m.group(2)
            name = m.group(3)
            sorted_addrs.append((addr, name))
            # ensure sym_info has entry (some S/D symbols may not be in -nm)
            if name not in sym_info:
                sym_info[name] = {"addr": addr, "section": "?", "type": sym_type}
            else:
                sym_info[name]["type"] = sym_type

    return sorted_addrs, sym_info


# ── Compute symbol sizes with section-boundary capping ────────────────

def compute_symbol_sizes(sorted_addrs, sections):
    """
    Compute estimated size for each symbol.

    Uses consecutive-address differencing, but caps each symbol at its
    section's end boundary.  This prevents the catastrophic overcounts
    that occur when the next visible symbol is in a different section
    (sometimes megabytes away).

    Returns: {name: size_bytes}
    """
    sizes = {}
    n = len(sorted_addrs)

    for idx in range(n):
        addr, name = sorted_addrs[idx]

        # find the end of the section this symbol lives in
        sec_end = find_section_end(addr, sections)

        if idx + 1 < n:
            next_addr = sorted_addrs[idx + 1][0]
            # cap at section boundary
            if sec_end is not None and next_addr > sec_end:
                raw_size = sec_end - addr
            else:
                raw_size = next_addr - addr
        else:
            # last symbol
            raw_size = (sec_end - addr) if sec_end else 0

        sizes[name] = max(raw_size, 0)

    return sizes


# ── Extract defined symbols from a .a file ────────────────────────────

def get_library_symbols(lib_path):
    """Return set of defined external symbol names in a .a file."""
    lines = run_tool(["nm", "-g", lib_path])
    symbols = set()
    for line in lines:
        m = re.match(r'^[0-9a-f]+\s+[A-Z]\s+(\S+)', line)
        if m:
            sym = m.group(1)
            if sym not in CPP_RUNTIME_SYMBOLS:
                symbols.add(sym)
    return symbols


# ── Resolve library path (macOS) ──────────────────────────────────────

def resolve_library_path(lib_info, platform_libs):
    """Resolve the actual .a path, preferring macOS platform overrides."""
    name = lib_info["name"]
    info = platform_libs.get(name, lib_info)
    path = info.get("lib", "")
    if not path or path.startswith("-framework") or path.startswith("-l"):
        return None
    return path if os.path.exists(path) else None


# ── Formatting helpers ────────────────────────────────────────────────

def fmt_size(b):
    """Human-readable: e.g. '8.0 MB', '340 KB', '42 B'."""
    if b >= 1024 * 1024:
        return f"{b / 1048576:.1f} MB"
    if b >= 1024:
        return f"{b / 1024:.0f} KB"
    return f"{b} B"


def fmt_tbl(b):
    """For Markdown table cells: '~340 KB'."""
    if b >= 1024 * 1024:
        return f"~{b / 1048576:.1f} MB"
    if b >= 1024:
        return f"~{b // 1024} KB"
    return f"~{b} B"


def classify_unclaimed(name):
    """Classify an unclaimed symbol by prefix.  Returns group or None."""
    for prefix, group in SYMBOL_PREFIX_GROUPS:
        if name.startswith(prefix):
            return group
    return None


# ── Main analysis ─────────────────────────────────────────────────────

def analyze(binary_path, config_path, verbose=False, top_n=5):
    if platform.system() != "Darwin":
        print("Error: this script requires macOS (uses otool, nm -nm, size -m).",
              file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(binary_path):
        print(f"Error: binary '{binary_path}' not found.", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(config_path):
        print(f"Error: config '{config_path}' not found.", file=sys.stderr)
        sys.exit(1)

    binary_size = os.path.getsize(binary_path)
    binary_name = os.path.basename(binary_path)
    print(f"Analyzing {binary_name}  ({fmt_size(binary_size)}, "
          f"{binary_size:,} bytes)", file=sys.stderr)

    # ── 1. Load build config ──────────────────────────────────────────
    with open(config_path) as f:
        config = json.load(f)

    platform_libs = {}
    for pl in config.get("platforms", {}).get("macos", {}).get("libraries", []):
        platform_libs[pl["name"]] = pl

    # ── 2. Parse binary ───────────────────────────────────────────────
    print("  Parsing sections...", file=sys.stderr)
    sections = get_sections(binary_path)

    print("  Building symbol map...", file=sys.stderr)
    sorted_addrs, sym_info = get_binary_symbols(binary_path)
    symbol_sizes = compute_symbol_sizes(sorted_addrs, sections)

    total_sym_size = sum(symbol_sizes.values())
    print(f"  {len(sorted_addrs)} visible symbols, "
          f"~{fmt_size(total_sym_size)} measured via nm", file=sys.stderr)

    # ── 3. Match .a symbols → binary ──────────────────────────────────
    print("  Matching library symbols...", file=sys.stderr)
    claimed = set()

    # Track per-group symbol details for verbose output
    group_sym_details = defaultdict(list)  # group -> [(name, size), ...]

    groups = defaultdict(lambda: {
        "size": 0, "symbols": 0, "lib_file_size": 0,
        "notes": [], "matched_count": 0, "libs": [],
    })

    for lib_info in config.get("libraries", []):
        name = lib_info["name"]
        group = LIBRARY_GROUPS.get(name, name)
        lib_path = resolve_library_path(lib_info, platform_libs)
        if lib_path is None:
            continue

        lib_file_size = os.path.getsize(lib_path)
        groups[group]["lib_file_size"] += lib_file_size
        if name not in groups[group]["libs"]:
            groups[group]["libs"].append(name)

        lib_syms = get_library_symbols(lib_path)
        matched = 0
        matched_size = 0
        for sym in lib_syms:
            if sym in sym_info and sym not in claimed:
                claimed.add(sym)
                matched += 1
                sz = symbol_sizes.get(sym, 0)
                matched_size += sz
                group_sym_details[group].append((sym, sz))

        groups[group]["matched_count"] += matched
        groups[group]["size"] += matched_size
        groups[group]["symbols"] += matched

    # ── 3b. Cap nm-measured sizes at .a file size ───────────────────
    # With LTO + symbol stripping, big gaps between visible symbols
    # can attribute code from OTHER libraries to one symbol.  The .a
    # file size is a hard upper bound on linked contribution (it's
    # usually much less due to dead-code elimination, but it prevents
    # gross overestimates).
    for group, d in groups.items():
        if d["lib_file_size"] > 0 and d["size"] > d["lib_file_size"]:
            d["notes"].append(
                f"capped: nm {fmt_tbl(d['size'])} > .a {fmt_tbl(d['lib_file_size'])}"
            )
            d["size"] = d["lib_file_size"]

    # ── 3c. Apply stub-link overrides ─────────────────────────────────
    for group, verified_size in STUB_LINK_OVERRIDES.items():
        if group in groups:
            old = groups[group]["size"]
            groups[group]["size"] = verified_size
            groups[group]["notes"].append(
                f"stub-link verified (nm estimated {fmt_tbl(old)})"
            )

    # ── 4. Handle LTO-invisible libraries ─────────────────────────────
    for lib_name, override in LTO_INVISIBLE_OVERRIDES.items():
        group = LIBRARY_GROUPS.get(lib_name, lib_name)
        if group in groups and groups[group]["matched_count"] > 0:
            continue  # has real matches, skip override
        if override["size"] > 0:
            groups[group]["size"] = override["size"]
        groups[group]["notes"].append(override["note"])

    # ── 5. Classify unclaimed symbols by prefix ───────────────────────
    prefix_extra = defaultdict(lambda: {"size": 0, "count": 0})
    project_size = 0
    project_count = 0

    for name in list(sym_info.keys()):
        if name in claimed:
            continue
        sz = symbol_sizes.get(name, 0)
        grp = classify_unclaimed(name)
        if grp:
            prefix_extra[grp]["size"] += sz
            prefix_extra[grp]["count"] += 1
            group_sym_details[grp].append((name, sz))
            claimed.add(name)
        else:
            project_size += sz
            project_count += 1

    for grp, data in prefix_extra.items():
        groups[grp]["size"] += data["size"]
        groups[grp]["symbols"] += data["count"]

    # ── 6. Segment totals (from `size -m`) ────────────────────────────
    size_lines = run_tool(["size", "-m", binary_path], timeout=10)
    segment_text = "\n".join(size_lines)
    text_seg = data_seg = linkedit_seg = 0
    for ln in size_lines:
        ln = ln.strip()
        if ln.startswith("Segment __TEXT:"):
            m = re.search(r'(\d+)', ln.replace(",", ""))
            if m: text_seg = int(m.group(1))
        elif ln.startswith("Segment __DATA_CONST:"):
            m = re.search(r'(\d+)', ln.replace(",", ""))
            if m: data_seg += int(m.group(1))
        elif ln.startswith("Segment __DATA:"):
            m = re.search(r'(\d+)', ln.replace(",", ""))
            if m: data_seg += int(m.group(1))
        elif ln.startswith("Segment __LINKEDIT:"):
            m = re.search(r'(\d+)', ln.replace(",", ""))
            if m: linkedit_seg = int(m.group(1))

    # ── 7. Project code = total content − library sum ─────────────────
    # With heavy LTO, most project symbols are invisible.  Instead of
    # relying on the few unclaimed symbols, compute project code as:
    #   (TEXT + DATA segments) − sum(library sizes) − overhead estimate
    lib_total = sum(g["size"] for g in groups.values())
    content_total = text_seg + data_seg  # actual code+data in binary
    overhead_est = linkedit_seg + 80 * 1024  # LINKEDIT + stubs/unwind/ObjC/alignment
    project_est = content_total - lib_total - overhead_est
    if project_est < 0:
        project_est = project_size  # fallback to unclaimed symbols

    groups["Project code + LTO-hidden"]["size"] = project_est
    groups["Project code + LTO-hidden"]["symbols"] = project_count
    groups["Project code + LTO-hidden"]["notes"].append(
        "= segments − libraries − overhead; includes LTO-internalized code"
    )

    # ── 8. Mach-O overhead ────────────────────────────────────────────
    all_accounted = sum(g["size"] for g in groups.values())
    overhead = binary_size - all_accounted
    if overhead > 0:
        groups["Mach-O overhead"]["size"] = overhead
        groups["Mach-O overhead"]["notes"].append(
            "LINKEDIT, stubs, unwind, ObjC metadata, alignment padding"
        )
    elif overhead < 0:
        # Overcounted — adjust Project code estimate down to compensate
        proj = groups["Project code + LTO-hidden"]
        proj["size"] = max(0, proj["size"] + overhead)
        overhead = binary_size - sum(g["size"] for g in groups.values())
        if overhead > 0:
            groups["Mach-O overhead"]["size"] = overhead
            groups["Mach-O overhead"]["notes"].append(
                "LINKEDIT, stubs, unwind, ObjC metadata, alignment padding"
            )

    # ── 9. Sort and render ────────────────────────────────────────────
    special = {"Project code + LTO-hidden", "Mach-O overhead"}
    lib_rows = [(k, v) for k, v in groups.items() if k not in special]
    lib_rows.sort(key=lambda x: x[1]["size"], reverse=True)

    # filter out zero-size groups that have no symbols and no notes
    lib_rows = [(k, v) for k, v in lib_rows
                if v["size"] > 0 or v["symbols"] > 0 or v["notes"]]

    all_rows = lib_rows
    for s in ["Project code + LTO-hidden", "Mach-O overhead"]:
        if s in groups and groups[s]["size"] > 0:
            all_rows.append((s, groups[s]))

    # ── Output ────────────────────────────────────────────────────────
    print()
    print(f"## Binary Size Analysis: {binary_name}")
    print()
    print(f"- **File size**: {fmt_size(binary_size)} ({binary_size:,} bytes)")
    print(f"- **__TEXT segment**: {fmt_size(text_seg)}")
    print(f"- **__DATA + __DATA_CONST**: {fmt_size(data_seg)}")
    print(f"- **__LINKEDIT**: {fmt_size(linkedit_seg)}")
    print(f"- **Visible symbols**: {len(sorted_addrs)}")
    print()
    print("| Group | Size | % | Symbols | .a File | Notes |")
    print("|-------|-----:|--:|--------:|--------:|-------|")

    grand = 0
    grand_syms = 0
    for name, d in all_rows:
        sz = d["size"]
        pct = sz / binary_size * 100 if binary_size else 0
        syms = d["symbols"]
        lsz = d["lib_file_size"]
        notes = "; ".join(d["notes"]) if d["notes"] else ""
        sym_s = str(syms) if syms > 0 else "—"
        lsz_s = fmt_tbl(lsz) if lsz > 0 else "—"
        print(f"| **{name}** | {fmt_tbl(sz)} | {pct:.1f}% "
              f"| {sym_s} | {lsz_s} | {notes} |")
        grand += sz
        grand_syms += syms

    pct_t = grand / binary_size * 100 if binary_size else 0
    print(f"| **TOTAL** | **{fmt_tbl(grand)}** | **{pct_t:.1f}%** "
          f"| {grand_syms} | — | |")
    print()

    # ── Verbose: top symbols per group ────────────────────────────────
    if verbose:
        print("### Top Symbols per Group (by size)")
        print()
        for name, d in all_rows:
            details = group_sym_details.get(name, [])
            if not details:
                continue
            details.sort(key=lambda x: x[1], reverse=True)
            print(f"**{name}** ({d['symbols']} symbols, {fmt_tbl(d['size'])}):")
            print()
            print("| Symbol | Size |")
            print("|--------|-----:|")
            for sym, sz in details[:top_n]:
                # strip leading underscore for readability
                display = sym[1:] if sym.startswith("_") else sym
                print(f"| `{display}` | {fmt_tbl(sz)} |")
            if len(details) > top_n:
                rest = sum(s for _, s in details[top_n:])
                print(f"| *... {len(details) - top_n} more* | *{fmt_tbl(rest)}* |")
            print()

    # ── Dynamic deps ──────────────────────────────────────────────────
    print("### Dynamic Dependencies")
    print()
    otool_lines = run_tool(["otool", "-L", binary_path], timeout=10)
    third_party = []
    system_fw = []
    for ln in otool_lines[1:]:
        ln = ln.strip()
        if not ln:
            continue
        path = ln.split("(")[0].strip()
        if "/usr/lib/" in path or "libSystem" in path:
            continue
        if ".framework/" in path:
            fw = path.split(".framework/")[0].split("/")[-1]
            system_fw.append(fw)
        else:
            third_party.append(path)
    if third_party:
        print("**Third-party dynamic libraries:**")
        for p in third_party:
            print(f"- `{p}`")
    else:
        print("No third-party dynamic library dependencies. ✅")
    if system_fw:
        print(f"\n**System frameworks:** {', '.join(system_fw)}")
    print()

    # ── Section detail ────────────────────────────────────────────────
    print("<details><summary>Segment / Section Breakdown</summary>")
    print()
    print("```")
    print(segment_text)
    print("```")
    print("</details>")

    return 0


# ── CLI ───────────────────────────────────────────────────────────────

def main():
    p = argparse.ArgumentParser(
        description="Analyze binary size contributions by library group."
    )
    p.add_argument("binary", nargs="?", default=DEFAULT_BINARY,
                   help=f"Binary to analyze (default: {DEFAULT_BINARY})")
    p.add_argument("--config", "-c", default=DEFAULT_CONFIG,
                   help="Path to build_lambda_config.json")
    p.add_argument("--verbose", "-v", action="store_true",
                   help="Show top symbols per group")
    p.add_argument("--top", "-n", type=int, default=5,
                   help="Number of top symbols to show per group (default: 5)")
    args = p.parse_args()
    return analyze(args.binary, args.config, args.verbose, args.top)


if __name__ == "__main__":
    sys.exit(main())
