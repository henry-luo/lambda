let bad = ["unsafe.tar", "unsafe-pax.tar", "unsafe-gnu.tar", "nul-pax.tar", "duplicate.tar", "conflict.tar",
    "bad-header.tar", "truncated.tar", "missing-end.tar", "trailing.tar", "symlink.tar", "hardlink.tar",
    "fifo.tar", "device.tar", "bad-crc.tar.gz", "truncated.tar.gz", "bad-crc.tar.bz2", "bad-crc.tar.xz",
    "bad-crc.tar.zst", "not-tar.gz", "7z-encrypted.7z", "7z-ppmd.7z", "7z-bcj2.7z",
    "rar-solid.rar", "rar-encrypted.rar", "rar-links.rar", "rar5-encrypted.rar", "rar5-links.rar", "rar5-volume.rar"]
let nested = input("test/input/archive/nested.tar", {max_nesting_depth: 0})^
let seven = input("test/input/archive/7z-solid.7z", {max_expanded_bytes: 130})^
let selected = [for (entry in content(seven)^ where entry.name == "file4") entry][0]
let earlier = [for (entry in content(seven)^ where entry.name == "file2") entry][0]
let text = input(selected, 'text')^;
[
    [for (file in bad) (input("test/input/archive/" ++ file) or "error") == "error"],
    (input("test/input/archive/legacy.rar", 'rar5') or "error") == "error",
    (input("test/input/archive/sample.tar.gz", {max_expanded_bytes: 10239}) or "error") == "error",
    (input("test/input/archive/sample.tar", {max_member_bytes: 1}) or "error") == "error",
    (input("test/input/archive/7z-lzma2.7z", {max_index_bytes: 32}) or "error") == "error",
    (input(content(nested)^[0], '7z') or "error") == "error",
    len(text) == 52,
    input(selected, 'text')^ == text,
    (input(earlier, 'text') or "error") == "error",
    (input(earlier, 'text') or "error") == "error"
]
