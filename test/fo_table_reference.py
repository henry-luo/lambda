#!/usr/bin/env python3
"""Compare admitted FO table, alignment and graphics fixtures with Apache FOP and Poppler.

Requires Pillow, pdftoppm, pdfinfo, a Java runtime and a binary FOP distribution.
Pass a FOP configuration that embeds the same Arial files Radiant resolves.
All generated artifacts default to temp/fo_table_reference under the checkout.
The alignment fixture includes relative-align, unsupported by FOP 2.11; its
strict comparison records that difference. display_alignment isolates supported
display and body alignment without changing the relative-alignment fixture.
The lists_context fixture retains the nearest-list-block function binding case;
FOP 2.11 reads the part's inherited provisional values instead and differs.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

from PIL import Image, ImageColor


def run(args, cwd, log=None):
    result = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=60)
    if log:
        log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError("Command failed: " + repr(args) + "\n" + result.stdout)
    return result.stdout


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def page_geometry(pdf, output):
    info = run(["pdfinfo", str(pdf)], output)
    pages = int(re.search(r"^Pages:\s+(\d+)$", info, re.MULTILINE).group(1))
    info = run(["pdfinfo", "-box", "-f", "1", "-l", str(pages), str(pdf)], output)
    boxes = re.findall(r"^Page\s*(?:\d+)?\s+MediaBox:\s+(.+)$", info, re.MULTILINE)
    rotations = re.findall(r"^Page\s*(?:\d+)?\s+rot:\s+(\d+)$", info, re.MULTILINE)
    if len(boxes) != pages or len(rotations) != pages:
        raise RuntimeError("Poppler did not report every physical page")
    return {"pages": pages, "boxes": [[float(v) for v in box.split()] for box in boxes],
            "rotations": [int(v) for v in rotations]}


def color_bounds(path, colors):
    with Image.open(path) as source:
        image = source.convert("RGB")
    data = image.load()
    result = {}
    for color in colors:
        value = ImageColor.getrgb(color)
        left, top, right, bottom, count = image.width, image.height, 0, 0, 0
        mask = bytearray(image.width * image.height)
        for y in range(image.height):
            for x in range(image.width):
                if data[x, y] == value:
                    left, top = min(left, x), min(top, y)
                    right, bottom = max(right, x + 1), max(bottom, y + 1)
                    count += 1
                    mask[y * image.width + x] = 1
        result[color] = {"box": [left, top, right, bottom] if count else None,
                         "pixels": count, "mask_sha256": hashlib.sha256(mask).hexdigest()}
    return {"size": list(image.size), "colors": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lambda-exe", type=Path, required=True)
    parser.add_argument("--java", type=Path, required=True)
    parser.add_argument("--fop-home", type=Path, required=True,
                        help="directory containing build/ and lib/")
    parser.add_argument("--fop-config", type=Path, required=True)
    parser.add_argument("--fixture", choices=("tables", "alignment", "display_alignment", "graphics", "lists", "lists_context", "cell_flow", "indents", "indents_auto", "corresponding", "conditional", "conditional_components", "conditional_visible", "proportions", "proportions_columns", "numbered_columns", "table_furniture"), default="tables")
    parser.add_argument("--output-dir", type=Path, default=Path("temp/fo_table_reference"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scratch = output / "java-scratch"
    scratch.mkdir(exist_ok=True)
    binary, java, home, config = [p.resolve() for p in
                                (args.lambda_exe, args.java, args.fop_home, args.fop_config)]
    fixture = root / ("test/html/paged_media_" + args.fixture + ".fo")
    source = ET.parse(fixture)
    color_properties = ("background-color", "fill") if args.fixture == "graphics" else (
        ("background-color", "border-before-color", "border-after-color", "border-start-color", "border-end-color")
        if args.fixture in ("corresponding", "conditional", "conditional_components", "conditional_visible") else ("background-color",))
    colors = sorted({node.attrib[prop] for node in source.iter() for prop in color_properties
                     if prop in node.attrib})
    repeated_colors = {node.attrib["background-color"] for node in source.iter()
                       if node.tag.rsplit("}", 1)[-1] in ("table-header", "table-footer", "table-column")
                       and "background-color" in node.attrib}
    terminal_colors = {}
    for table in source.iter():
        if table.tag.rsplit("}", 1)[-1] != "table":
            continue
        for kind, property_name, page in (("table-header", "table-omit-header-at-break", 1),
                                          ("table-footer", "table-omit-footer-at-break", 3)):
            if table.attrib.get(property_name) != "true":
                continue
            for node in table:
                if node.tag.rsplit("}", 1)[-1] == kind and "background-color" in node.attrib:
                    color = node.attrib["background-color"]
                    repeated_colors.discard(color)
                    terminal_colors[color] = page
    if not colors:
        raise RuntimeError("The fixture must contain observable colored regions")
    java_args = [str(java), "-XX:-UsePerfData", "-Djava.awt.headless=true", "-Djava.io.tmpdir=" + str(scratch),
                 "-cp", str(home / "build/*") + os.pathsep + str(home / "lib/*"),
                 "org.apache.fop.cli.Main"]
    report = {"fixture": args.fixture, "fixture_sha256": digest(fixture), "lambda_sha256": digest(binary),
              "fop_config_sha256": digest(config),
              "java": run([str(java), "-version"], output),
              "fop": run(java_args + ["-version"], output),
              "poppler": run(["pdftoppm", "-v"], output), "hosts": {}}
    reference = output / "fop.pdf"
    candidate = output / "radiant.pdf"
    run(java_args + ["-c", str(config), "-fo", str(fixture), "-pdf", str(reference)],
        root, output / "fop.log")
    run(java_args + ["-c", str(config), "-fo", str(fixture), "-at", "application/pdf", str(output / "fop-area-tree.xml")],
        root, output / "fop-area-tree.log")
    run([str(binary), "render", str(fixture), "--paged", "--block-remote-resources",
         "-o", str(candidate)], binary.parent, output / "radiant.log")
    for host, pdf in (("fop", reference), ("radiant", candidate)):
        geometry = page_geometry(pdf, output)
        if geometry["pages"] != 3:
            raise RuntimeError(host + " did not produce the required three pages")
        prefix = output / host
        run(["pdftoppm", "-r", "96", "-png", str(pdf), str(prefix)], output)
        masks = [color_bounds(output / (host + "-" + str(page) + ".png"), colors)
                 for page in range(1, geometry["pages"] + 1)]
        report["hosts"][host] = {"geometry": geometry, "masks": masks, "pdf_sha256": digest(pdf)}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    fop, radiant = report["hosts"]["fop"], report["hosts"]["radiant"]
    if fop["geometry"] != radiant["geometry"]:
        raise RuntimeError("Physical page boxes or rotations differ; see report.json")
    for page, (expected, actual) in enumerate(zip(fop["masks"], radiant["masks"]), 1):
        if expected["size"] != actual["size"]:
            raise RuntimeError("Raster dimensions differ on page " + str(page))
        for color in colors:
            if color in repeated_colors and expected["colors"][color]["box"] is None:
                raise RuntimeError("Reference omitted a repeated region on page " + str(page))
            if color in terminal_colors and (expected["colors"][color]["box"] is not None) != (page == terminal_colors[color]):
                raise RuntimeError("Reference conditional furniture occurs on an incorrect page " + str(page))
            if expected["colors"][color]["box"] != actual["colors"][color]["box"]:
                raise RuntimeError("Region bounds differ for " + color + " on page " + str(page))
            if args.fixture == "graphics" and expected["colors"][color]["mask_sha256"] != actual["colors"][color]["mask_sha256"]:
                raise RuntimeError("Graphic color masks differ for " + color + " on page " + str(page))
    print("PASS: three physical pages and every colored region bound match Apache FOP" +
          ("; graphics masks match exactly" if args.fixture == "graphics" else ""))


if __name__ == "__main__":
    main()
