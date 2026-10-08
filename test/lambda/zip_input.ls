let archive = input("test/input/zip/sample.docx")^
let children = content(archive)^
let word = children[0]
let part = content(word)^[0]
let json = children[1]
let raw = input(children[2], 'binary')^
let empty = children[3]
let nested = input(children[5], 'zip')^;
[
    type(archive) == element,
    archive.kind == 'file',
    archive.format == 'zip',
    archive.entry_path == "",
    len(children),
    word.kind == 'dir',
    part.entry_path == "word/document.xml",
    input(json)^.answer,
    content(json)^[0].answer,
    len(raw),
    len(content(empty)^),
    len(input(empty, 'text')^),
    input(empty, 'binary')^ == null,
    type(input(children[5])^) == binary,
    input(content(nested)^[0], 'text')^ == "nested",
    input(content(input("test/input/zip/wide.zip")^)^[0], 'text')^ == "ZIP64",
    input(children[6], 'binary')^ != null,
    input("test/input/zip/renamed.dat")^.format == 'zip',
    type(input("test/input/zip/sample.docx", 'binary')^) == binary,
    len(content(input("test/input/zip/empty.zip")^)^),
    input("test/input/zip/extensionless")^.format == 'zip',
    content(input("test/input/zip/office.docx")^)^[0].name == "[Content_Types].xml",
    input(content(content(input("test/input/zip/office.docx")^)^[3])^[0], 'text')^ == "unicode"
]
