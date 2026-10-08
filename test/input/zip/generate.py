"""Deterministic ZIP fixtures; run from the repository root. No external packages."""
from pathlib import Path
from io import BytesIO
import zipfile
import struct
import zlib

base = Path(__file__).parent

def archive(entries):
    data = BytesIO()
    with zipfile.ZipFile(data, 'w') as z:
        z.comment = b'ZIP fixture comment'
        for name, payload, method in entries:
            info = zipfile.ZipInfo(name, (2020, 1, 2, 3, 4, 6))
            info.compress_type = method
            z.writestr(info, payload)
    return data.getvalue()

nested = archive([('inner.txt', b'nested', 8)])
normal = archive([
    ('word/document.xml', b'<document><text>Hello</text></document>', 8),
    ('word/_rels/document.xml.rels', b'<?xml version="1.0"?><Relationships/>', 8),
    ('data.json', b'{"answer":42}', 8),
    ('opaque.bin', b'\x00\xffPK\x00', 0),
    ('empty.txt', b'', 0),
    ('empty-dir/', b'', 0),
    ('nested.zip', nested, 8),
    ('bad.json', b'{invalid', 8),
])
(base / 'sample.docx').write_bytes(normal)
(base / 'renamed.dat').write_bytes(normal)
(base / 'extensionless').write_bytes(normal)
(base / 'legacy.doc').write_bytes(bytes.fromhex('d0cf11e0a1b11ae1') + b'legacy compound container')
# A minimal Open XML Word package, plus a binary PNG part.
import base64
office = archive([
    ('[Content_Types].xml', b'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="png" ContentType="image/png"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/></Types>', 8),
    ('_rels/.rels', b'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>', 8),
    ('word/document.xml', b'<?xml version="1.0"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:body><w:p><w:r><w:t>Hello</w:t></w:r></w:p><w:sectPr/></w:body></w:document>', 8),
    ('word/media/pixel.png', base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aEAAAAABJRU5ErkJggg=='), 0),
    ('notes/\u00e9 space.txt', b'unicode', 8),
])
(base / 'office.docx').write_bytes(office)
(base / 'empty.zip').write_bytes(archive([]))
corrupt = bytearray(normal)
corrupt[struct.unpack_from('<H', normal, 26)[0] + 30] ^= 0x80
(base / 'corrupt.zip').write_bytes(corrupt)
(base / 'traversal.zip').write_bytes(archive([('../escape', b'no', 0)]))
# Real ZIP64 local + central + EOCD records, using a small payload.
name, payload = b'wide.txt', b'ZIP64'
crc = zlib.crc32(payload)
extra = struct.pack('<HHQQ', 1, 16, len(payload), len(payload))
local = struct.pack('<IHHHHHIIIHH', 0x04034b50, 45, 0x800, 0, 0, 33, crc, 0xffffffff, 0xffffffff, len(name), len(extra)) + name + extra + payload
central = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 45, 45, 0x800, 0, 0, 33, crc, 0xffffffff, 0xffffffff, len(name), len(extra), 0, 0, 0, 0, 0) + name + extra
end64 = struct.pack('<IQHHIIQQQQ', 0x06064b50, 44, 45, 45, 0, 0, 1, 1, len(central), len(local))
locator = struct.pack('<IIQI', 0x07064b50, 0, len(local) + len(central), 1)
end = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, 0xffff, 0xffff, 0xffffffff, 0xffffffff, 0)
(base / 'wide.zip').write_bytes(local + central + end64 + locator + end)
# Descriptor variants include an embedded signature in the stored payload.
for signed in (False, True):
    name, payload = b'descriptor.bin', b'PK\x07\x08\x00data'
    crc = zlib.crc32(payload)
    local = struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 8, 0, 0, 33, 0, 0, 0, len(name), 0) + name + payload
    descriptor = (struct.pack('<I', 0x08074b50) if signed else b'') + struct.pack('<III', crc, len(payload), len(payload))
    central = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, 8, 0, 0, 33, crc, len(payload), len(payload), len(name), 0, 0, 0, 0, 0, 0) + name
    end = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, 1, 1, len(central), len(local) + len(descriptor), 0)
    (base / ('descriptor-signed.zip' if signed else 'descriptor.zip')).write_bytes(local + descriptor + central + end)
    # ZIP64 uses 64-bit descriptor sizes even for these small payloads.
    extra = struct.pack('<HHQQ', 1, 16, 0, 0)
    local = struct.pack('<IHHHHHIIIHH', 0x04034b50, 45, 8, 0, 0, 33, 0, 0xffffffff, 0xffffffff, len(name), len(extra)) + name + extra + payload
    descriptor = (struct.pack('<I', 0x08074b50) if signed else b'') + struct.pack('<IQQ', crc, len(payload), len(payload))
    extra = struct.pack('<HHQQ', 1, 16, len(payload), len(payload))
    central = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 45, 45, 8, 0, 0, 33, crc, 0xffffffff, 0xffffffff, len(name), len(extra), 0, 0, 0, 0, 0) + name + extra
    end = struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, 1, 1, len(central), len(local) + len(descriptor), 0)
    (base / ('descriptor64-signed.zip' if signed else 'descriptor64.zip')).write_bytes(local + descriptor + central + end)
