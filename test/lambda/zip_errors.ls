fn counted(x: element) int | error => len(x)
fn walked(x) => for (v in x) v
let archive = input("test/input/zip/corrupt.zip")^
let word = content(archive)^[0]
let broken = content(word)^[0]
let bad = content(input("test/input/zip/sample.docx")^)^[6];
[
    len(content(archive)^) == 7,
    len(content(word)^) == 2,
    ((content(broken) ^ { null }) or "error") == "error",
    ((content(broken) ^ { null }) or "error") == "error",
    ((len(broken) ^ { null }) or "error") == "error",
    ((input(broken, 'binary') ^ { null }) or "error") == "error",
    ((input(bad, 'json') ^ { null }) or "error") == "error",
    type(input(bad, 'binary')^) == binary,
    ((input("test/input/zip/traversal.zip") ^ { null }) or "error") == "error",
    ((input("test/input/zip/wide.zip", {max_member_bytes: 2}) ^ { null }) or "error") == "error",
    ((input("test/input/zip/empty.zip", {max_entries: -1}) ^ { null }) or "error") == "error",
    ((input(word, 'text') ^ { null }) or "error") == "error",
    (broken[0] ^ { "error" }) == "error",
    (broken?string ^ { "error" }) == "error",
    ((format(broken, 'mark') ^ { null }) or "error") == "error",
    ((counted(broken) ^ { null }) or "error") == "error",
    ((walked(broken) ^ { null }) or "error") == "error",
    ((input("test/input/zip/legacy.doc", 'zip') ^ { null }) or "error") == "error"
]
