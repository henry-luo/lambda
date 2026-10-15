fn member(tree, name) => [for (entry in content(tree)^ where entry.name == name) entry][0]
pn main() {
    let tar = input("test/input/archive/sample.tar.xz")^
    output(tar, "temp/archive-tar-copy.zip", {format: 'zip', deterministic: true})^
    let copy = input("temp/archive-tar-copy.zip")^
    let seven = input("test/input/archive/7z-solid.7z")^
    output(seven, "temp/archive-seven-copy.zip", 'zip')^
    let modern = input("test/input/archive/rar5-stored.rar")^
    output(modern, "temp/archive-rar-copy.zip", 'zip')^;
    [
        len(content(copy)^) == 6,
        input(member(member(copy, "docs"), "data.json"))^.answer == 42,
        input(member(copy, "opaque.bin"), 'binary')^ == b'\x00FF4100',
        input(member(copy, "hello.txt"), 'text')^ == input(member(tar, "hello.txt"), 'text')^,
        input(member(input("temp/archive-seven-copy.zip")^, "file4"), 'binary')^ == input(member(seven, "file4"), 'binary')^,
        input(member(input("temp/archive-rar-copy.zip")^, "helloworld.txt"), 'text')^ == "hello libarchive test suite!\n"
    ]
}
