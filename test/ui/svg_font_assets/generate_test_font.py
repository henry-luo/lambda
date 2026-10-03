"""Generate a deterministic rectangular SVG test face, including non-BMP cmap entries.

The glyph geometry and SFNT tables are authored here; no external font is copied.
Run from the repository root with python3 test/ui/svg_font_assets/generate_test_font.py.
"""
from pathlib import Path
import struct


def pack(fmt, *args):
    return struct.pack('>' + fmt, *args)


def checksum(data):
    padded = data + b'\0' * (-len(data) % 4)
    return sum(struct.unpack('>' + 'I' * (len(padded) // 4), padded)) & 0xffffffff


tables = {}
tables['head'] = pack('IIIIHHQQhhhhHHhhh', 0x10000, 0x10000, 0, 0x5f0f3cf5,
    11, 1000, 0, 0, 0, -200, 1000, 800, 0, 8, 2, 1, 0)
tables['hhea'] = pack('IhhhHhhhhhhhhhhhH', 0x10000, 800, -200, 0, 1000,
    0, 0, 1000, 1, 0, 0, 0, 0, 0, 0, 0, 3)
tables['maxp'] = pack('I14H', 0x10000, 3, 4, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
tables['hmtx'] = pack('HhHhHh', 1000, 0, 1000, 0, 1000, 0)
# glyphs 0 (.notdef) and 1 (space) are empty; glyph 2 is one rectangular contour.
glyph = pack('hhhhhHH', 1, 0, -200, 1000, 800, 3, 0) + b'\1' * 4
for deltas in ((0, 1000, 0, -1000), (-200, 0, 1000, 0)):
    glyph += pack('4h', *deltas)
tables['glyf'] = glyph
tables['loca'] = pack('4I', 0, 0, 0, len(glyph))
tables['post'] = pack('IihhIIIII', 0x30000, 0, -100, 50, 0, 0, 0, 0, 0)
# Horizontal metrics use a 1000-unit em with a 500-unit x-height.
os2 = bytearray(96)
def field(offset, fmt, *args):
    value = pack(fmt, *args); os2[offset:offset + len(value)] = value
field(0, 'HhHHH', 3, 1000, 400, 5, 0)
field(10, '8h', 650, 650, 0, 140, 650, 650, 0, 350)
field(26, 'hhh', 50, 300, 0)
field(42, '4I', 1, 0, 0, 0)
os2[58:62] = b'LAMB'
field(62, '3H3h2H', 0x40, 32, 0x3a9, 800, -200, 0, 800, 200)
field(86, 'hhHHH', 500, 800, 0, 32, 1)
tables['OS/2'] = bytes(os2)
points = {32: 1, **{cp: 2 for cp in range(65, 91)}, 0xe9: 2, 0x3a9: 2,
    0x10400: 2, 0x1d11e: 2}
groups = b''.join(pack('III', cp, cp, gid) for cp, gid in sorted(points.items()))
cmap = pack('HHIII', 12, 0, 16 + len(groups), 0, len(points)) + groups
tables['cmap'] = pack('HHHHIHHI', 0, 2, 0, 4, 20, 3, 10, 20) + cmap
names = {0: 'Lambda SVG rectangular test fixture', 1: 'SVG Test Rectangle',
    2: 'Regular', 4: 'SVG Test Rectangle Regular', 6: 'SVGTestRectangle-Regular'}
records = b''; strings = b''
for name_id, value in names.items():
    value = value.encode('utf-16-be')
    records += pack('6H', 3, 1, 0x409, name_id, len(value), len(strings))
    strings += value
tables['name'] = pack('3H', 0, len(names), 6 + len(records)) + records + strings
count = len(tables); power = 1 << (count.bit_length() - 1)
header = pack('I4H', 0x10000, count, power * 16, power.bit_length() - 1, count * 16 - power * 16)
directory = b''; data = b''; head_offset = 0
for tag, content in sorted(tables.items()):
    offset = len(header) + count * 16 + len(data)
    directory += tag.encode('ascii') + pack('III', checksum(content), offset, len(content))
    data += content + b'\0' * (-len(content) % 4)
    if tag == 'head': head_offset = offset
font = bytearray(header + directory + data)
font[head_offset + 8:head_offset + 12] = pack('I', (0xb1b0afba - checksum(font)) & 0xffffffff)
assert checksum(font) == 0xb1b0afba
Path(__file__).with_name('rectangle.ttf').write_bytes(font)
