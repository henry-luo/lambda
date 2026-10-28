"""Check unchanged reference bytes and independently read their saved slide text."""
from pathlib import Path
import argparse
import hashlib
import json
import posixpath
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).parent
NS = {"p": "http://schemas.openxmlformats.org/presentationml/2006/main",
      "a": "http://schemas.openxmlformats.org/drawingml/2006/main"}
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"


def oracle(record):
    path = ROOT / record["file"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == record["sha256"], path
    with zipfile.ZipFile(path) as archive:
        presentation = ET.fromstring(archive.read("ppt/presentation.xml"))
        relationships = {node.get("Id"): posixpath.normpath("ppt/" + node.get("Target"))
                         for node in ET.fromstring(archive.read("ppt/_rels/presentation.xml.rels"))}
        slides = [relationships[node.get("{" + R + "}id")]
                  for node in presentation.findall("p:sldIdLst/p:sldId", NS)]
        text = "".join(node.text or "" for slide in slides
                       for node in ET.fromstring(archive.read(slide)).findall(".//a:t", NS))
        size = presentation.find("p:sldSz", NS)
        return {"file": record["file"], "slides": len(slides),
                "width": int(size.get("cx")) / 9525, "height": int(size.get("cy")) / 9525,
                "text": text}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--update", action="store_true")
    args = parser.parse_args()
    rows = [oracle(record) for record in json.loads((ROOT / "manifest.json").read_text())["files"]]
    if args.update:
        (ROOT / "oracle.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n")
    else:
        assert rows == json.loads((ROOT / "oracle.json").read_text()), "reference oracle differs"
    print(f"Verified {len(rows)} reference hashes and saved slide-text records")
