"""Validate emitted byte offsets and stream extents independently of PDF intake."""
import re
import sys
import zlib
from pathlib import Path


def validate(path, expected_pages=None, expected_box=(0, 0, 150, 90)):
    data = Path(path).read_bytes()
    assert data.startswith(b"%PDF-1.4\n"), "missing PDF header"
    footer = re.search(rb"startxref\n([0-9]+)\n%%EOF\n$", data)
    assert footer, "missing PDF footer"
    position = int(footer[1])
    lines = data[position:].splitlines()
    assert lines[0] == b"xref", "startxref does not address the cross-reference table"
    first, count = map(int, lines[1].split())
    assert first == 0 and count > 4
    assert lines[2] == b"0000000000 65535 f "
    assert lines[2 + count] == b"trailer", "incorrect cross-reference entry count"
    offsets = []
    for identifier, line in enumerate(lines[3:2 + count], 1):
        assert re.fullmatch(rb"[0-9]{10} 00000 n ", line), "malformed live entry"
        offset = int(line[:10])
        assert data[offset:].startswith(f"{identifier} 0 obj\n".encode()), "object offset mismatch"
        offsets.append(offset)
    assert offsets == sorted(offsets) and len(set(offsets)) == len(offsets)
    objects = [data[begin:end] for begin, end in zip(offsets, offsets[1:] + [position])]
    for body in objects:
        assert body.endswith(b"endobj\n"), "incomplete indirect object"
        stream = re.search(rb"/Length ([0-9]+) >>\nstream\n", body)
        if stream:
            ending = stream.end() + int(stream[1])
            assert body[ending:] == b"\nendstream\nendobj\n", "stream length mismatch"
            if b"/Filter /FlateDecode" in body[:stream.end()]:
                zlib.decompress(body[stream.end():ending])
    if expected_pages is None:
        assert any(b"/Subtype /Image" in body and b"/SMask " in body for body in objects), "missing image alpha mask"
        assert any(b"/ColorSpace /DeviceGray" in body for body in objects), "missing alpha image"
        assert any(b"/Type /ExtGState" in body for body in objects), "missing transparent fill state"
    else:
        pages = [body for body in objects if re.search(rb"/Type /Page\b", body)]
        assert len(pages) == expected_pages, "incorrect physical page count"
        tree = next(body for body in objects if b"/Type /Pages " in body)
        kids = re.search(rb"/Kids \[([^]]*)\] /Count ([0-9]+)", tree)
        assert kids and int(kids[2]) == expected_pages, "incorrect page tree count"
        identifiers = [int(value) for value in re.findall(rb"([0-9]+) 0 R", kids[1])]
        assert len(identifiers) == expected_pages and len(set(identifiers)) == expected_pages
        for identifier in identifiers:
            page = objects[identifier - 1]
            assert re.search(rb"/Type /Page\b", page), "page tree addresses a nonpage object"
            if expected_box is not None:
                box = "/MediaBox [" + " ".join(map(str, expected_box)) + "]"
                assert box.encode() in page, "CSS-to-point page geometry mismatch"
            content = re.search(rb"/Contents ([0-9]+) 0 R", page)
            assert content and b"\nstream\n" in objects[int(content[1]) - 1], "missing page content stream"
    print(f"PDF_OUTPUT_CHECK: {count - 1} object offsets and stream lengths verified")
    return objects


def decoded_content(objects):
    programs = []
    for body in objects:
        stream = re.search(rb"/Length ([0-9]+) >>\nstream\n", body)
        if stream and b"/Subtype /Image" not in body:
            raw = body[stream.end():stream.end() + int(stream[1])]
            programs.append(zlib.decompress(raw) if b"/Filter /FlateDecode" in body else raw)
    return b"".join(programs)


def phase6(path):
    import subprocess
    from PIL import Image

    objects = validate(path, 1, None)
    data = Path(path).read_bytes()
    for title in ["PDF phase 6 Ω", "Chapter Ω", "Section 中😀"]:
        encoded = (b"\xfe\xff" + title.encode("utf-16-be")).hex().upper().encode()
        assert b"<" + encoded + b">" in data, "Unicode metadata/outline encoding mismatch"
    catalog = objects[0]
    root_id = int(re.search(rb"/Outlines ([0-9]+) 0 R", catalog)[1])
    root = objects[root_id - 1]
    chapter_id = int(re.search(rb"/First ([0-9]+) 0 R", root)[1])
    chapter = objects[chapter_id - 1]
    section_id = int(re.search(rb"/First ([0-9]+) 0 R", chapter)[1])
    assert f"/Parent {chapter_id} 0 R".encode() in objects[section_id - 1]
    assert b"/Count 2" in root and b"/Count 1" in chapter
    assert any(b"/Dest (gradient)" in body and b"/Subtype /Link" in body for body in objects)
    assert any(b"/URI (https://example.com/?a=1&b=2)" in body for body in objects)
    content = decoded_content(objects)
    assert b"5 5 m\n80 5 l\n45 45 l\nh\nS\n" in content, "SVG stroke lost its vector geometry"
    assert b"[5 4] 1.5 d" in content, "dashed outline lost its stroke pattern"
    info = subprocess.run(["pdfinfo", path], check=True, capture_output=True, text=True).stdout
    assert "PDF phase 6 Ω" in info and "Lambda PDF Renderer" in info
    prefix = str(Path(path).with_suffix("")) + "_checked"
    subprocess.run(["pdftoppm", "-singlefile", "-r", "96", path, prefix], check=True, capture_output=True)
    image = Image.open(prefix + ".ppm").convert("RGB")
    samples = {(35, 35): (32, 64, 224), (95, 25): (232, 0, 22), (145, 25): (22, 0, 232),
               (185, 30): (255, 127, 127), (215, 30): (127, 127, 255), (265, 30): (255, 255, 255),
               (45, 160): (0, 160, 64), (45, 145): (32, 48, 224), (35, 95): (32, 176, 96),
               (202, 87): (127, 104, 0), (236, 105): (255, 208, 0), (225, 142): (255, 0, 0), (225, 160): (0, 0, 255), (240, 195): (0, 0, 0)}
    for position, expected in samples.items():
        actual = image.getpixel(position)
        assert max(abs(a - b) for a, b in zip(actual, expected)) <= 3, (position, actual, expected)
    print("PDF_PHASE6_CHECK: Unicode, outline hierarchy, links, SVG strokes and 14 independent raster samples verified")


if __name__ == "__main__":
    if len(sys.argv) > 3 and sys.argv[2] == "content":
        pages = len(re.findall(rb"/Type /Page\b", Path(sys.argv[1]).read_bytes()))
        objects = validate(sys.argv[1], pages, None)
        needle = sys.argv[3].replace(r"\n", "\n").encode()
        sys.exit(0 if needle in decoded_content(objects) else 1)
    elif len(sys.argv) > 2 and sys.argv[2] == "phase6":
        phase6(sys.argv[1])
    else:
        validate(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else None)
