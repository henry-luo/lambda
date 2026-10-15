#!/usr/bin/env python3
"""Reproduce archive fixtures using Python and the pinned libarchive release."""
import binascii
import bz2
import gzip
import hashlib
import io
import lzma
from pathlib import Path
import subprocess
import sys
import struct
import tarfile

ROOT = Path(__file__).resolve().parents[3]
DEST = Path(__file__).resolve().parent
UPSTREAM = {
    '7z-copy.7z': 'test_read_format_7zip_copy.7z',
    '7z-lzma.7z': 'test_read_format_7zip_lzma1.7z',
    '7z-lzma2.7z': 'test_read_format_7zip_lzma2.7z',
    '7z-delta.7z': 'test_read_format_7zip_delta_lzma2.7z',
    '7z-bcj.7z': 'test_read_format_7zip_bcj_lzma2.7z',
    '7z-skip.7z': 'test_read_format_7zip_extract_second.7z',
    '7z-solid.7z': 'test_read_format_7zip_lzma1_2.7z',
    '7z-copy-multi.7z': 'test_read_format_7zip_copy_2.7z',
    '7z-ppmd.7z': 'test_read_format_7zip_ppmd.7z',
    '7z-bcj2.7z': 'test_read_format_7zip_bcj2_lzma2_1.7z',
    'rar-solid.rar': 'test_read_format_rar4_solid_encrypted.rar',
    'rar5-volume.rar': 'test_read_format_rar5_multiarchive_solid.part01.rar',
    '7z-empty.7z': 'test_read_format_7zip_empty_archive.7z',
    '7z-empty-file.7z': 'test_read_format_7zip_empty_file.7z',
    '7z-encrypted.7z': 'test_read_format_7zip_encryption_header.7z',
    'legacy.rar': 'test_read_format_rar_windows.rar',
    'rar-links.rar': 'test_read_format_rar.rar',
    'rar-encrypted.rar': 'test_read_format_rar_encryption_data.rar',
    'rar5-stored.rar': 'test_read_format_rar5_stored.rar',
    'rar5-compressed.rar': 'test_read_format_rar5_compressed.rar',
    'rar5-solid.rar': 'test_read_format_rar5_solid.rar',
    'rar5-blake2.rar': 'test_read_format_rar5_blake2.rar',
    'rar5-unicode.rar': 'test_read_format_rar5_unicode.rar',
    'rar5-encrypted.rar': 'test_read_format_rar5_encrypted_filenames.rar',
    'rar5-links.rar': 'test_read_format_rar5_symlink.rar',
}


def tar_bytes(format, entries):
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode='w', format=format) as archive:
        for name, payload in entries:
            item = tarfile.TarInfo(name)
            item.mtime = 1700000000
            item.mode = 0o755 if payload is None else 0o640
            if payload is None:
                item.type = tarfile.DIRTYPE
                archive.addfile(item)
            else:
                item.size = len(payload)
                archive.addfile(item, io.BytesIO(payload))
    return data.getvalue()


def rar5_empty(crc):
    # Minimal single-volume Stored archive: main, empty member and end headers.
    def block(body):
        assert len(body) < 128
        content = bytes([len(body)]) + body
        return struct.pack('<I', binascii.crc32(content)) + content
    return (b'Rar!\x1a\x07\x01\x00' + block(bytes([1, 0, 0])) +
            block(bytes([2, 2, 0, 4, 0]) + bytes([0xa4, 0x83, 0x02]) + struct.pack('<I', crc) + bytes([0, 1, 5]) + b'empty') +
            block(bytes([5, 0, 0])))


def seven_copy_solid():
    # A plain 7z header describing four substreams in one Copy folder.
    def uint(value):
        for count in range(8):
            if value < 1 << (7 + 7 * count):
                prefix = (0xff << (8 - count)) & 0xff
                return bytes([prefix | (value >> (8 * count))]) + (value & ((1 << (8 * count)) - 1)).to_bytes(count, 'little')
        return b'\xff' + value.to_bytes(8, 'little')
    files = [b''.join(bytes([97 + line]) * 12 + b'\n' for line in range(count)) for count in range(1, 5)]
    payload = b''.join(files)
    header = b'\x01\x04\x06' + uint(0) + uint(1) + b'\x09' + uint(len(payload)) + b'\x00'
    header += b'\x07\x0b\x01\x00\x01\x01\x00\x0c' + uint(len(payload)) + b'\x00'
    header += b'\x08\x0d\x04\x09' + b''.join(uint(len(data)) for data in files[:-1])
    header += b'\x0a\x01' + b''.join(struct.pack('<I', binascii.crc32(data)) for data in files) + b'\x00\x00'
    names = b'\x00' + ''.join('file%d\0' % i for i in range(1, 5)).encode('utf-16le')
    header += b'\x05\x04\x11' + uint(len(names)) + names + b'\x00\x00'
    start = struct.pack('<QQI', len(payload), len(header), binascii.crc32(header))
    return b"7z\xbc\xaf\x27\x1c\x00\x04" + struct.pack('<I', binascii.crc32(start)) + start + payload + header


def main():
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'temp/archive-vendor/libarchive-3.8.10.tar.xz'
    assert hashlib.sha256(source.read_bytes()).hexdigest() == '56cad765459d5ee25a3b761f90c47d99379d28297fbfb72d908880d6b875fb96'
    with tarfile.open(source) as archive:
        archive_copying = archive.extractfile('libarchive-3.8.10/COPYING').read()
        for output, name in UPSTREAM.items():
            encoded = archive.extractfile('libarchive-3.8.10/libarchive/test/' + name + '.uu').read().splitlines()
            begin = next(i for i, line in enumerate(encoded) if line.startswith(b'begin '))
            decoded = b''.join(binascii.a2b_uu(line) for line in encoded[begin + 1:] if line != b'end')
            (DEST / output).write_bytes(decoded)
    (DEST / 'COPYING').write_bytes(archive_copying)
    (DEST / '7z-solid-copy.7z').write_bytes(seven_copy_solid())
    (DEST / 'rar5-empty.rar').write_bytes(rar5_empty(0))
    (DEST / 'rar5-empty-bad-crc.rar').write_bytes(rar5_empty(1))
    entries = [('./', None), ('docs/data.json', b'{"answer":42}'), ('hello.txt', b'hello archive\n'),
               ('opaque.bin', b'\0\xffA\0'), ('empty', b''), ('empty-dir', None), ('caf\u00e9/\u4e2d\u6587.txt', 'Unicode'.encode())]
    basic = tar_bytes(tarfile.PAX_FORMAT, entries)
    (DEST / 'sample.tar').write_bytes(basic)
    (DEST / 'sample.tar.gz').write_bytes(gzip.compress(basic, mtime=0))
    (DEST / 'sample.tar.bz2').write_bytes(bz2.compress(basic))
    (DEST / 'sample.tar.xz').write_bytes(lzma.compress(basic))
    subprocess.run(['zstd', '-q', '-f', str(DEST / 'sample.tar'), '-o', str(DEST / 'sample.tar.zst')], check=True)
    for name, format in [('ustar', tarfile.USTAR_FORMAT), ('gnu', tarfile.GNU_FORMAT)]:
        names = [('data.json', b'{"answer":42}'), ('empty', b''), ('empty-dir', None)]
        if name == 'gnu':
            names.append(('long/' + 'n' * 160 + '.txt', b'long name'))
        (DEST / (name + '.tar')).write_bytes(tar_bytes(format, names))
    (DEST / 'empty.tar').write_bytes(tar_bytes(tarfile.USTAR_FORMAT, []))
    (DEST / 'nested.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [
        ('inside.7z', (DEST / '7z-copy.7z').read_bytes()), ('inside.rar', (DEST / 'rar5-stored.rar').read_bytes())]))
    (DEST / 'unsafe.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [('../escape', b'bad')]))
    (DEST / 'duplicate.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [('a', b'1'), ('./a', b'2')]))
    (DEST / 'conflict.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [('a', b'1'), ('a/b', b'2')]))
    (DEST / 'unsafe-pax.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [('safe/' + 'x' * 120 + '/../escape', b'bad')]))
    (DEST / 'unsafe-gnu.tar').write_bytes(tar_bytes(tarfile.GNU_FORMAT, [('safe/' + 'x' * 120 + '/../escape', b'bad')]))
    # Preserve distinct Unicode spellings without filesystem normalization.
    (DEST / 'unicode.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [
        ('café.txt', b'NFC'), ('café.txt', b'NFD')]))
    (DEST / 'nul-pax.tar').write_bytes(tar_bytes(tarfile.PAX_FORMAT, [('safe\0../bad' + 'x' * 110, b'bad')]))
    for name, kind in [('symlink', tarfile.SYMTYPE), ('hardlink', tarfile.LNKTYPE),
                       ('fifo', tarfile.FIFOTYPE), ('device', tarfile.CHRTYPE)]:
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode='w', format=tarfile.PAX_FORMAT) as archive:
            item = tarfile.TarInfo('special'); item.type = kind; item.linkname = 'target'
            archive.addfile(item)
        (DEST / (name + '.tar')).write_bytes(output.getvalue())
    sparse_data = io.BytesIO()
    with tarfile.open(fileobj=sparse_data, mode='w', format=tarfile.PAX_FORMAT) as archive:
        item = tarfile.TarInfo('sparse'); item.size = 1
        item.pax_headers = {'GNU.sparse.map': '0,1', 'GNU.sparse.size': '1024', 'GNU.sparse.numblocks': '1'}
        archive.addfile(item, io.BytesIO(b'x'))
    (DEST / 'sparse.tar').write_bytes(sparse_data.getvalue())
    v7 = bytearray(tar_bytes(tarfile.USTAR_FORMAT, [('hello.txt', b'v7')]))
    v7[257:512] = bytes(255)
    v7[148:156] = b' ' * 8
    v7[148:156] = ('%06o\0 ' % sum(v7[:512])).encode()
    (DEST / 'v7.tar').write_bytes(v7)
    (DEST / 'truncated.tar').write_bytes(basic[:512])
    (DEST / 'missing-end.tar').write_bytes(basic.rstrip(b'\0'))
    (DEST / 'trailing.tar').write_bytes(basic + b'x' + bytes(511))
    (DEST / 'concat.tar.gz').write_bytes(gzip.compress(basic[:4096], mtime=0) + gzip.compress(basic[4096:], mtime=0))
    (DEST / 'not-tar.gz').write_bytes(gzip.compress(b'ordinary gzip payload', mtime=0))
    for codec in ['bz2', 'xz', 'zst']:
        corrupt = bytearray((DEST / ('sample.tar.' + codec)).read_bytes())
        corrupt[-4 if codec == 'bz2' else -8] ^= 1
        (DEST / ('bad-crc.tar.' + codec)).write_bytes(corrupt)
    checksum = bytearray(basic)
    checksum[0] ^= 1
    (DEST / 'bad-header.tar').write_bytes(checksum)
    corrupted = bytearray((DEST / 'sample.tar.gz').read_bytes())
    corrupted[-8] ^= 1
    (DEST / 'bad-crc.tar.gz').write_bytes(corrupted)
    (DEST / 'truncated.tar.gz').write_bytes((DEST / 'sample.tar.gz').read_bytes()[:-4])
    print('Generated archive fixtures in', DEST)


if __name__ == '__main__':
    main()
