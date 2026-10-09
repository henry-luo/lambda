#!/usr/bin/env python3
"""Derive image sizes and sprite-sheet definitions from the pinned resources."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from import_upstream import REVISION, verify_source

DEMO = Path(__file__).resolve().parents[1]


def generate(source):
    verify_source(source)
    metadata = {"revision": REVISION, "images": {}, "sheets": {}, "weapons": {}, "sources": []}
    for path in sorted((DEMO / "assets").rglob("*.png")):
        payload = path.read_bytes()
        if payload[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"not PNG: {path}")
        width, height = struct.unpack(">II", payload[16:24])
        metadata["images"][path.relative_to(DEMO).as_posix()] = {"width": width, "height": height}
    for relative in ["src/renderer/scene/entities/sprites.css", "src/renderer/scene/entities/enemies.css",
                     "src/renderer/scene/entities/decorations.css", "src/renderer/scene/entities/things.css",
                     "src/ui/weapons.css", "src/ui/spectator.css"]:
        path = source / relative
        text = path.read_text()
        metadata["sources"].append({"path": relative, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        for selector, body in re.findall(r'((?:\.sprite|#weapon)\[data-type="[^"]+"\]|#player\s*>\s*\.sprite)\s*\{([^}]+)', text):
            name_match = re.search(r'data-type="([^"]+)"', selector)
            name = name_match[1] if name_match else "player"
            image = re.search(r"background-image:\s*url\(['\"]?/?([^'\")]+)", body)
            if not image:
                continue
            dimensions = {key: int(value) for key, value in re.findall(r"--(w|h|frames|rows|cols):\s*(\d+)", body)}
            dimensions.update({"path": image[1], "rows": dimensions.get("rows", 1),
                               "cols": dimensions.get("cols", dimensions["frames"])})
            metadata["weapons" if selector.startswith("#weapon") else "sheets"][name] = dimensions
    relative = "src/renderer/scene/entities/sprites.js"
    text = (source / relative).read_text()
    layout = re.search(r"const SPRITE_LAYOUT = \{(.*?)\n\};", text, re.S)[1]
    metadata["sources"].append({"path": relative, "sha256": hashlib.sha256((source / relative).read_bytes()).hexdigest()})
    metadata["layouts"] = {type_id: {key: int(value) for key, value in re.findall(r"(\w+):\s*(-?\d+)", body)}
                           for type_id, body in re.findall(r"(\d+):\s*\{([^}]+)\}", layout)}
    verify_metadata(metadata)
    (DEMO / "data/images.json").write_text(json.dumps(metadata, indent=2) + "\n")


def verify_metadata(metadata):
    if metadata["revision"] != REVISION:
        raise ValueError("image metadata revision differs from pinned upstream")
    for relative, dimensions in metadata["images"].items():
        width, height = struct.unpack(">II", (DEMO / relative).read_bytes()[16:24])
        if dimensions != {"width": width, "height": height}:
            raise ValueError(f"image metadata dimensions disagree: {relative}")
    for category in ["sheets", "weapons"]:
        for name, sheet in metadata[category].items():
            image = metadata["images"][sheet["path"]]
            if image != {"width": sheet["w"] * sheet["cols"], "height": sheet["h"] * sheet["rows"]}:
                raise ValueError(f"sheet dimensions disagree: {name}")
    for name in ["soulsphere", "health-bonus", "armor-bonus", "green-armor", "blue-armor", "invisibility", "player"]:
        if name not in metadata["sheets"]:
            raise ValueError(f"missing animated sprite definition: {name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    generate(args.source)
