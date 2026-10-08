fn counted(x: element) int | error => len(x)
fn walked(x) => for (v in x) v
let archive = input("test/input/zip/corrupt.zip")^
let word = content(archive)^[0]
let broken = content(word)^[0]
let bad = content(input("test/input/zip/sample.docx")^)^[6];
[
    len(content(archive)^) == 7,
    len(content(word)^) == 2,
    (content(broken) or "error") == "error",
    (content(broken) or "error") == "error",
    (len(broken) or "error") == "error",
    (input(broken, 'binary') or "error") == "error",
    (input(bad, 'json') or "error") == "error",
    type(input(bad, 'binary')^) == binary,
    (input("test/input/zip/traversal.zip") or "error") == "error",
    (input("test/input/zip/wide.zip", {max_member_bytes: 2}) or "error") == "error",
    (input("test/input/zip/empty.zip", {max_entries: -1}) or "error") == "error",
    (input(word, 'text') or "error") == "error",
    (broken[0] or "error") == "error",
    (broken?string or "error") == "error",
    (format(broken, 'mark') or "error") == "error",
    (counted(broken) or "error") == "error",
    (walked(broken) or "error") == "error",
    (input("test/input/zip/legacy.doc", 'zip') or "error") == "error"
]
