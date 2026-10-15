fn member(tree, name) => [for (entry in content(tree)^ where entry.name == name) entry][0]
let tar = input("test/input/archive/sample.tar")^
let seven = input("test/input/archive/7z-solid.7z", '7z')^
let legacy = input("test/input/archive/legacy.rar", 'rar')^
let modern = input("test/input/archive/rar5-stored.rar", 'rar')^
let nested = input("test/input/archive/nested.tar")^
let inside = input(member(nested, "inside.7z"), '7z')^;
[
    tar.format == 'tar',
    len(content(tar)^) == 6,
    input(member(member(tar, "docs"), "data.json"))^.answer == 42,
    input(member(tar, "hello.txt"), 'text')^ == "hello archive\n",
    input(member(tar, "opaque.bin"), 'binary')^ == b'\x00FF4100',
    len(content(member(tar, "empty"))^) == 0,
    len(content(member(tar, "empty-dir"))^) == 0,
    member(tar, "hello.txt").compressed_size == null,
    member(tar, "hello.txt").compression == null,
    member(tar, "hello.txt").crc32 == null,
    input(member(member(tar, "café"), "中文.txt"), 'text')^ == "Unicode",
    seven.format == '7z',
    input(member(seven, "file4"), 'text')^ == "aaaaaaaaaaaa\nbbbbbbbbbbbb\ncccccccccccc\ndddddddddddd\n",
    legacy.format == 'rar',
    input(member(legacy, "test.txt"), 'text')^ == "test text file\r\n",
    modern.format == 'rar5',
    input(member(modern, "helloworld.txt"), 'text')^ == "hello libarchive test suite!\n",
    input("test/input/archive/rar5-stored.rar", 'rar5')^.format == 'rar5',
    type(input(member(nested, "inside.7z"))^) == binary,
    inside.format == '7z',
    len(input(content(inside)^[0], 'binary')^) == 60,
    input(member(nested, "inside.rar"), 'rar')^.format == 'rar5',
    type(input("test/input/archive/sample.tar.xz", 'binary')^) == binary,
    [for (file in ["sample.tar.gz", "sample.tar.bz2", "sample.tar.xz", "sample.tar.zst", "concat.tar.gz"])
        input(member(input("test/input/archive/" ++ file)^, "hello.txt"), 'text')^ == "hello archive\n"]
]
