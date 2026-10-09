#!/usr/bin/env python3
"""Import the pinned cssDOOM resources; never used by the running demo."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

DEMO = Path(__file__).resolve().parents[1]
REVISION = "438d2e17fb9f75fa3dbc54e1d21a3bc088ad009d"
REPOSITORY = "https://github.com/NielsLeenheer/cssDOOM"


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def constants(source):
    # Evaluate only the two import-free constant tables, with no game/browser code.
    modules = [source / "src/game/constants.js", source / "src/renderer/scene/constants.js"]
    for module in modules:
        if any(line.lstrip().startswith("import ") for line in module.read_text().splitlines()):
            raise ValueError(f"constant module gained imports: {module}")
    program = """
const tables = await Promise.all(process.argv.slice(1).map(url => import(url)));
process.stdout.write(JSON.stringify(tables, (_, value) =>
    value instanceof Set ? [...value] : value === Infinity ? "infinite" : value));
"""
    result = subprocess.run(["node", "--input-type=module", "-e", program,
                             *[module.as_uri() for module in modules]],
                            check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def verify_source(source):
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError(f"expected upstream {REVISION}, got {revision}")
    changes = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True)
    if changes:
        raise ValueError("upstream checkout must be clean to preserve provenance")


def import_resources(source):
    verify_source(source)
    records = []
    sources = [(source / "LICENSE.txt", "LICENSE.cssDOOM.txt")]
    for root, destination in [("public/maps", "data/maps"), ("public/assets", "assets")]:
        sources.extend((path, f"{destination}/{path.relative_to(source / root).as_posix()}")
                       for path in sorted((source / root).rglob("*")) if path.is_file())
    for original, relative in sources:
        destination = DEMO / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, destination)
        payload = destination.read_bytes()
        records.append({"source": original.relative_to(source).as_posix(), "path": relative,
                        "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
                        "provenance": "upstream-code-license" if relative.startswith("LICENSE")
                                      else "upstream-bundled-doom-resource"})
    rules, visuals = constants(source)
    write_json(DEMO / "data/rules.json", rules)
    write_json(DEMO / "data/visuals.json", visuals)
    from generate_visual_metadata import generate
    generate(source)
    maps = []
    for path in sorted((DEMO / "data/maps").glob("*.json")):
        data = json.loads(path.read_text())
        maps.append({"name": path.stem, "path": f"maps/{path.name}",
                     "counts": {key: len(value) for key, value in data.items() if isinstance(value, list)},
                     "thing_types": dict(sorted(Counter(str(thing["type"]) for thing in data["things"]).items())),
                     "sector_specials": sorted({sector["specialType"] for sector in data["sectors"]}),
                     "linedef_specials": sorted({line["specialType"] for line in data["linedefs"]})})
    write_json(DEMO / "data/catalog.json", {"schema_version": 1, "repository": REPOSITORY,
               "revision": REVISION, "maps": maps, "rules": "rules.json", "visuals": "visuals.json",
               "manifest": "resources.json", "nonfinite_constant_encoding": {"infinite": "positive infinity"}})
    write_json(DEMO / "data/resources.json", {"schema_version": 1, "revision": REVISION, "files": records})
    metadata = json.loads((DEMO / "upstream.json").read_text())
    metadata["resources"]["status"] = "imported and hashed"
    metadata["resources"]["manifest"] = "data/resources.json"
    write_json(DEMO / "upstream.json", metadata)
    print(f"Imported {len(maps)} maps and {len(records)} exact upstream files ({sum(r['bytes'] for r in records):,} bytes)")


def verify_resources():
    manifest = json.loads((DEMO / "data/resources.json").read_text())
    if manifest["revision"] != REVISION:
        raise ValueError("resource manifest revision differs from pinned upstream")
    for record in manifest["files"]:
        path = DEMO / record["path"]
        payload = path.read_bytes()
        if len(payload) != record["bytes"] or hashlib.sha256(payload).hexdigest() != record["sha256"]:
            raise ValueError(f"resource integrity mismatch: {record['path']}")
    catalog = json.loads((DEMO / "data/catalog.json").read_text())
    if [entry["name"] for entry in catalog["maps"]] != [f"E1M{i}" for i in range(1, 10)]:
        raise ValueError("episode catalog must contain E1M1 through E1M9")
    from generate_visual_metadata import verify_metadata
    verify_metadata(json.loads((DEMO / "data/images.json").read_text()))
    print(f"Verified {len(manifest['files'])} resource hashes and the nine-map catalog")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path, help="clean pinned upstream checkout")
    parser.add_argument("--verify", action="store_true", help="verify checked-in resources without upstream or Node")
    options = parser.parse_args()
    if options.verify:
        verify_resources()
    elif options.source:
        import_resources(options.source.resolve())
        verify_resources()
    else:
        parser.error("provide an upstream checkout or --verify")
