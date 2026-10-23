"""Keep DOCX rendering structure while shrinking raster payloads for regression fixtures."""
from pathlib import Path
import argparse
import xml.etree.ElementTree as ET
import zipfile


def minimize(source, destination):
    raster_types = {"image/png", "image/jpeg", "image/gif", "image/bmp", "image/tiff", "image/webp"}
    # EMF previews retain their saved records; raster payloads reuse the generated test pixel.
    with zipfile.ZipFile(Path(__file__).with_name("legacy-graphics.docx")) as fixture:
        pixel = fixture.read("word/media/pixel.png")
    with zipfile.ZipFile(source) as archive:
        types = ET.fromstring(archive.read("[Content_Types].xml"))
        defaults = {node.attrib["Extension"]: node.attrib["ContentType"] for node in types
                    if node.tag.endswith("}Default")}
        overrides = {node.attrib["PartName"].lstrip("/"): node.attrib["ContentType"] for node in types
                     if node.tag.endswith("}Override")}
        entries = {}
        for name in archive.namelist():
            if name.startswith(("word/embeddings/", "word/fonts/")):
                continue
            mime = overrides.get(name, defaults.get(name.rsplit(".", 1)[-1]))
            entries[name] = pixel if mime in raster_types else archive.read(name)
        for node in types:
            if node.attrib.get("ContentType") in raster_types:
                node.set("ContentType", "image/png")
        entries["[Content_Types].xml"] = ET.tostring(types, encoding="utf-8", xml_declaration=True)
    with zipfile.ZipFile(destination, "w") as archive:
        for name, value in sorted(entries.items()):
            info = zipfile.ZipInfo(name, (2020, 1, 2, 3, 4, 6))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, value)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    minimize(args.source, args.destination)
