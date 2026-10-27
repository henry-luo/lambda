#!/usr/bin/env python3
"""Record the macos-ime source, link, and bundle closure under ./temp/."""

import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "temp/ime_chinese/m5"
LUA = ROOT / "premake5.ime.macos.lua"
MAP = WORK / "LambdaChineseIME.map"
APP = WORK / "LambdaChineseIME.app"
REPORT = WORK / "audit.json"


def main() -> None:
    generated = LUA.read_text()
    project = generated.split('\nproject "LambdaChineseIME"', 1)[1].split("\nproject ", 1)[0]
    files = project.split("    files {", 1)[1].split("    }", 1)[0]
    sources = re.findall(r'"([^"]+)"', files)
    objects = []
    for line in MAP.read_text(errors="replace").splitlines():
        if line == "# Sections:":
            break
        match = re.match(r"\[\s*\d+\] (.+)", line)
        if match:
            objects.append(match.group(1))
    archives = sorted({match.group(1) for item in objects
        if (match := re.search(r"([^/]+\.a)\(", item))})
    link_inputs = sorted(set(re.findall(r'"([^"]+\.a)"', project)))
    linked = subprocess.check_output(["otool", "-L", str(APP / "Contents/MacOS/LambdaChineseIME")],
                                     text=True).splitlines()[1:]
    subprocess.run(["codesign", "--verify", "--strict", str(APP)], check=True)
    forbidden_roots = ("lambda/js/", "lambda/dom/", "radiant/", "lambda/jube/",
                       "lambda/network/")
    forbidden_exact = {
        "lib/sqlite/sqlite3.c", "lib/rdb.c", "lib/rdb_sqlite.c",
        "lib/rdb_batch.c", "lib/rdb_tunnel.c", "lib/mail_imap.c",
        "lib/mail_mime.c", "lib/trust_store.c", "lib/secret_store.c",
        "lib/mime-detect.c", "lib/zlib_exact.c", "lib/line_framer.c",
        "lambda/runtime/mcp_module.cpp", "lambda/runtime/pdf_builder.cpp",
        "lambda/runtime/rdb_host.cpp", "lambda/runtime/io_secret.cpp",
        "lambda/runtime/io_loopback.cpp", "lambda/runtime/wasm_embed.cpp",
        "lambda/runtime/radiant_event_hook.cpp",
        "lambda/runtime/terminal_host.cpp",
    }
    residual_roots = ("lib/physics/", "lib/grep/", "lib/fts/", "lambda/input/",
                      "lambda/format/", "lambda/io/")
    allowed_resource_sources = {
        "lambda/input/input.cpp", "lambda/input/input-context.cpp",
        "lambda/input/input-utils.cpp", "lambda/input/input-json.cpp",
        "lambda/input/input-script-cache.cpp", "lambda/input/css/css_symbol_hook.cpp",
        "lambda/input/parse_error.cpp", "lambda/input/source_tracker.cpp",
        "lambda/format/format.cpp", "lambda/format/format-json.cpp",
        "lambda/format/format-text.cpp", "lambda/format/format-utils.cpp",
        "lambda/io/collection_io.cpp", "lambda/io/mark_builder.cpp",
        "lambda/io/target.cpp", "lambda/io/mark_editor.cpp",
    }
    forbidden_sources = [source for source in sources
                         if source.startswith(forbidden_roots) or source in forbidden_exact]
    residual_counts = {root: sum(source.startswith(root) for source in sources)
                       for root in residual_roots}
    unrelated_sources = [source for source in sources if source.startswith(residual_roots)
                         and source not in allowed_resource_sources]
    forbidden_archive_tokens = ("curl", "thorvg", "image-codecs", "openjp2",
        "mbedtls", "mbedx509", "archive-readers", "libbz2", "libbrotli",
        "liblzma", "libzstd", "libz.a")
    forbidden_archives = [name for name in archives if any(token in name.lower()
        for token in forbidden_archive_tokens)]
    forbidden_link_inputs = [path for path in link_inputs if any(token in path.lower()
        for token in forbidden_archive_tokens)]
    executable = APP / "Contents/MacOS/LambdaChineseIME"
    report = {
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "source_count": len(sources), "object_count": len(objects),
        "forbidden_sources": forbidden_sources,
        "forbidden_archives": forbidden_archives,
        "forbidden_link_inputs": forbidden_link_inputs,
        "residual_source_counts": residual_counts,
        "unrelated_sources": unrelated_sources,
        "linked_archives": archives,
        "link_inputs": link_inputs,
        "dynamic_dependencies": [entry.strip() for entry in linked],
        "strict_closure_pass": not forbidden_sources and not forbidden_archives and
            not forbidden_link_inputs and not unrelated_sources,
    }
    WORK.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"sources={len(sources)} objects={len(objects)} "
          f"forbidden={len(forbidden_sources) + len(forbidden_archives) + len(forbidden_link_inputs)} "
          f"strict={report['strict_closure_pass']} report={REPORT}")


if __name__ == "__main__":
    main()
