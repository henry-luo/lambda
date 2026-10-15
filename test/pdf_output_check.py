"""Validate emitted byte offsets and stream extents independently of PDF intake."""
import re
import sys
from pathlib import Path


def validate(path):
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
    assert any(b"/Subtype /Image" in body and b"/SMask " in body for body in objects), "missing image alpha mask"
    assert any(b"/ColorSpace /DeviceGray" in body for body in objects), "missing alpha image"
    assert any(b"/Type /ExtGState" in body for body in objects), "missing transparent fill state"
    print(f"PDF_OUTPUT_CHECK: {count - 1} object offsets and stream lengths verified")


if __name__ == "__main__":
    validate(sys.argv[1])
