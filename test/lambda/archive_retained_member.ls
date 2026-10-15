fn retain(path, name) {
    let root = input("test/input/archive/" ++ path)^;
    [for (entry in content(root)^ where entry.name == name) entry][0]
}
let tar = retain("sample.tar.xz", "hello.txt")^
let seven = retain("7z-solid.7z", "file4")^
let legacy = retain("legacy.rar", "test.txt")^
let modern = retain("rar5-stored.rar", "helloworld.txt")^
let churn = [for (i in 1 to 200) {index: i, text: string(i)}];
[
    input(tar, 'text')^ == "hello archive\n",
    len(input(seven, 'binary')^) == 52,
    input(legacy, 'text')^ == "test text file\r\n",
    input(modern, 'text')^ == "hello libarchive test suite!\n",
    len(churn) == 200
]
