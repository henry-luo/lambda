"""Validate emitted byte offsets and stream extents independently of PDF intake."""
import re
import sys
import zlib
from pathlib import Path


def validate(path, expected_pages=None, expected_box=(0, 0, 150, 90), quiet=False):
    data = Path(path).read_bytes()
    assert data.startswith((b"%PDF-1.4\n", b"%PDF-1.5\n", b"%PDF-1.6\n")), "missing PDF header"
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
    if not quiet:
        print(f"PDF_OUTPUT_CHECK: {count - 1} object offsets and stream lengths verified")
    return objects


def decoded_content(objects):
    programs = []
    contents = {int(match) for body in objects for match in re.findall(rb"/Contents ([0-9]+) 0 R", body)}
    for identifier, body in enumerate(objects, 1):
        stream = re.search(rb"/Length ([0-9]+) >>\nstream\n", body)
        if stream and (identifier in contents or b"/Subtype /Form" in body):
            raw = body[stream.end():stream.end() + int(stream[1])]
            programs.append(zlib.decompress(raw) if b"/Filter /FlateDecode" in body else raw)
    return b"".join(programs)


def roundtrip(source, destination):
    """Check regenerated PDFs with an independent reader, including every page."""
    import subprocess

    def inspect(path):
        result = subprocess.run(["pdfinfo", "-box", "-f", "1", "-l", "2147483647", str(path)],
                                check=True, capture_output=True, text=True, timeout=30)
        pages = int(re.search(r"^Pages:\s+(\d+)$", result.stdout, re.M)[1])
        geometry = {}
        for match in re.finditer(r"^Page\s+(\d+)\s+(rot|MediaBox|CropBox|BleedBox|TrimBox|ArtBox):\s*(.*)$",
                                 result.stdout, re.M):
            key = (int(match[1]), match[2])
            assert key not in geometry, "duplicate page geometry"
            geometry[key] = tuple(map(float, match[3].split()))
        assert pages > 0 and len(geometry) == pages * 6, "missing page geometry"
        assert {page for page, _ in geometry} == set(range(1, pages + 1)), "incomplete page sequence"
        return pages, geometry

    source, destination = Path(source), Path(destination)
    pages, expected = inspect(source)
    actual_pages, actual = inspect(destination)
    assert actual_pages == pages, f"lost pages: {pages} -> {actual_pages}"
    assert actual.keys() == expected.keys(), "lost page boxes or rotation"
    # pdfinfo reports box coordinates rounded to two decimal places.
    for key, coordinates in expected.items():
        assert len(actual[key]) == len(coordinates) and all(
            abs(left - right) <= 0.02 for left, right in zip(coordinates, actual[key])), (
                f"changed page geometry {key}: {coordinates} -> {actual[key]}")
    assert source.read_bytes() != destination.read_bytes(), "roundtrip bypassed PDF generation"
    validate(destination, pages, None, quiet=True)
    # one Poppler process checks all content streams without retaining full-size page images.
    prefix = str(destination.with_suffix("")) + "_checked"
    for raster in destination.parent.glob(destination.stem + "_checked-*.png"):
        raster.unlink()
    subprocess.run(["pdftoppm", "-scale-to", "128", "-png", str(destination), prefix],
                   check=True, capture_output=True, timeout=60)
    rasters = list(destination.parent.glob(destination.stem + "_checked-*.png"))
    assert len(rasters) == pages, f"rasterized {len(rasters)} of {pages} pages"
    for raster in rasters:
        raster.unlink()
    print(f"PDF_ROUNDTRIP_CHECK: {pages} pages, page boxes/rotation, object offsets and streams verified")


def text_export(path, expected_pages):
    import subprocess

    objects = validate(path, expected_pages, None)
    assert Path(path).read_bytes().startswith((b"%PDF-1.5\n", b"%PDF-1.6\n")), "ActualText requires PDF 1.5 or newer"
    content = decoded_content(objects)
    assert b"BT\n" in content and b"> Tj\n" in content, "text was not emitted with PDF text operators"
    fonts = [body for body in objects if b"/Type /Font /Subtype /Type0" in body]
    assert fonts and all(b"/ToUnicode " in font for font in fonts), "missing Unicode text font"
    assert any(b"/FontFile2 " in body for body in objects), "selected TrueType face was not embedded"
    assert len(fonts) < 5, "font was embedded separately for each text fragment"
    if expected_pages > 1:
        pages = [body for body in objects if re.search(rb"/Type /Page\b", body)]
        keys = [set(re.findall(rb"/F[0-9]+(?:S[0-9]+)? ([0-9]+) 0 R", page)) for page in pages]
        shared = set.intersection(*keys)
        assert shared, "pages do not reuse their common font resource"
        generated = {str(i).encode() for i, body in enumerate(objects, 1) if b"/Subtype /Type3" in body}
        if generated:
            assert shared & generated, "generated glyph fonts were embedded again for the next page"
    extracted = subprocess.run(["pdftotext", "-layout", path, "-"], check=True, capture_output=True, text=True).stdout
    phrases = ["Hello café Ω ∑ 𝔸", "Decorated text", "Affine text", "Synthetic italic", "Fallback 中文 😀", "Group text", "Overlap"]
    # continuous inline SVG is supported; paged flow currently omits that replaced box.
    if expected_pages == 1:
        phrases.append("SVG text Ω")
    else:
        phrases.extend(["Second page text", "Shared font resource", "Shared fallback 中文 😀"])
    for phrase in phrases:
        assert phrase in extracted, f"lost searchable text: {phrase}"
    assert extracted.count("Hello café Ω ∑ 𝔸") == 1, "logical text was emitted twice"
    print("PDF_TEXT_CHECK: embedded fonts, Unicode extraction, affine/decorated/SVG/group text and shared page fonts verified")


def read_ppm(path):
    """Read a binary (P6) PPM as written by pdftoppm: (width, height, rgb bytes)."""
    data = Path(path).read_bytes()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":  # a header comment runs to the end of its line
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    assert fields[0] == b"P6", "pdftoppm did not write a binary PPM"
    width, height, maxval = (int(field) for field in fields[1:])
    assert maxval == 255, "only 8-bit PPM samples are supported"
    pixels = data[pos + 1:]  # one whitespace byte separates the header from the samples
    assert len(pixels) >= width * height * 3, "truncated PPM"
    return width, height, pixels


def ppm_pixel(image, x, y):
    width, _, pixels = image
    offset = (y * width + x) * 3
    return tuple(pixels[offset:offset + 3])


def phase6(path):
    """Phase 6 semantics read from the bytes: no external tool is needed."""
    objects = validate(path, 1, None)
    data = Path(path).read_bytes()
    for title in ["PDF phase 6 Ω", "Chapter Ω", "Section 中😀"]:
        encoded = (b"\xfe\xff" + title.encode("utf-16-be")).hex().upper().encode()
        assert b"<" + encoded + b">" in data, "Unicode metadata/outline encoding mismatch"
    assert b"/Producer (Lambda PDF Renderer)" in data, "missing producer metadata"
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
    print("PDF_PHASE6_CHECK: Unicode, producer, outline hierarchy, links and SVG strokes verified")
    return objects


def phase6_raster(path):
    """Independent rendering by poppler; run when a phase 6 golden is accepted."""
    import subprocess

    info = subprocess.run(["pdfinfo", path], check=True, capture_output=True, text=True).stdout
    assert "PDF phase 6 Ω" in info and "Lambda PDF Renderer" in info
    prefix = str(Path(path).with_suffix("")) + "_checked"
    subprocess.run(["pdftoppm", "-singlefile", "-r", "96", path, prefix], check=True, capture_output=True)
    image = read_ppm(prefix + ".ppm")
    samples = {(35, 35): (32, 64, 224), (95, 25): (232, 0, 22), (145, 25): (22, 0, 232),
               (185, 30): (255, 127, 127), (215, 30): (127, 127, 255), (265, 30): (255, 255, 255),
               (45, 160): (0, 160, 64), (45, 145): (32, 48, 224), (35, 95): (32, 176, 96),
               (202, 87): (127, 104, 0), (236, 105): (255, 208, 0), (225, 142): (255, 0, 0), (225, 160): (0, 0, 255), (240, 195): (0, 0, 0)}
    for position, expected in samples.items():
        actual = ppm_pixel(image, *position)
        assert max(abs(a - b) for a, b in zip(actual, expected)) <= 3, (position, actual, expected)
    print("PDF_PHASE6_RASTER: pdfinfo metadata and 14 independent poppler raster samples verified")


def normalized_objects(objects):
    """Object bodies with each Flate stream inflated and its /Length dropped, so two
    builds that differ only in zlib's compressed bytes compare equal."""
    normalized = []
    for body in objects:
        stream = re.search(rb"/Length ([0-9]+) >>\nstream\n", body)
        if not stream:
            normalized.append(body)
            continue
        raw = body[stream.end():stream.end() + int(stream[1])]
        header = body[:stream.start()]
        decoded = zlib.decompress(raw) if b"/Filter /FlateDecode" in header else raw
        normalized.append(header + b"/Length * >>\nstream\n" + decoded)
    return normalized


def structure(path, kind, quiet=False):
    """The structural check selected by `kind`: a page count, "phase6", or None."""
    if kind == "phase6":
        return validate(path, 1, None, quiet=quiet)
    return validate(path, int(kind) if kind else None, quiet=quiet)


def check_semantics(path, kind):
    return phase6(path) if kind == "phase6" else structure(path, kind)


def compare_golden(path, golden, kind):
    """Check `path`, then require the same objects as the accepted golden PDF."""
    objects = normalized_objects(check_semantics(path, kind))
    expected = normalized_objects(structure(golden, kind, quiet=True))
    refresh = f"accept the new output with: python3 test/pdf_output_check.py {path} accept {golden}" + (
        f" {kind}" if kind else "")
    assert len(objects) == len(expected), (
        f"object count {len(objects)} differs from golden {len(expected)}; {refresh}")
    for identifier, (actual, wanted) in enumerate(zip(objects, expected), 1):
        if actual != wanted:
            at = next((i for i, (a, b) in enumerate(zip(actual, wanted)) if a != b), min(len(actual), len(wanted)))
            raise AssertionError(f"object {identifier} differs from golden at byte {at}: "
                                 f"{actual[max(0, at - 20):at + 40]!r} vs {wanted[max(0, at - 20):at + 40]!r}; {refresh}")
    print(f"PDF_GOLDEN_CHECK: {len(objects)} objects match {golden}")


def accept_golden(path, golden, kind):
    """Verify a new output (with poppler for phase 6) and store it as the golden."""
    import shutil

    check_semantics(path, kind)
    if kind == "phase6":
        phase6_raster(path)
    shutil.copyfile(path, golden)
    print(f"PDF_GOLDEN_ACCEPTED: {path} -> {golden}")

if __name__ == "__main__":
    if len(sys.argv) > 3 and sys.argv[2] == "roundtrip":
        roundtrip(sys.argv[1], sys.argv[3])
    elif len(sys.argv) > 3 and sys.argv[2] == "content":
        pages = len(re.findall(rb"/Type /Page\b", Path(sys.argv[1]).read_bytes()))
        objects = validate(sys.argv[1], pages, None)
        needle = sys.argv[3].replace(r"\n", "\n").encode()
        sys.exit(0 if needle in decoded_content(objects) else 1)
    elif len(sys.argv) > 3 and sys.argv[2] == "text":
        text_export(sys.argv[1], int(sys.argv[3]))
    elif len(sys.argv) > 3 and sys.argv[2] in ("golden", "accept"):
        kind = sys.argv[4] if len(sys.argv) > 4 else None
        (compare_golden if sys.argv[2] == "golden" else accept_golden)(sys.argv[1], sys.argv[3], kind)
    elif len(sys.argv) > 2 and sys.argv[2] == "phase6":
        phase6(sys.argv[1])
    elif len(sys.argv) > 2 and sys.argv[2] == "phase6-raster":
        phase6_raster(sys.argv[1])
    else:
        validate(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else None)
