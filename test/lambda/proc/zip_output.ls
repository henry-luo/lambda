pn main() {
    let source = input("test/input/zip/sample.docx")^
    output(source, "temp/zip-copy.zip", {format: 'zip', zip64: true, deterministic: true})^
    let copy = input("temp/zip-copy.zip")^
    let members = content(copy)^
    let json = [for (m in members where m.name == "data.json") m][0]
    let raw = [for (m in members where m.name == "opaque.bin") m][0]
    let tree = <fs kind: 'dir',
        <fs name: "hello.txt", kind: 'file', "hello">
        <fs name: "empty", kind: 'file'>
        <fs name: "data.json", kind: 'file', format: 'json', {answer: 42}>
        <fs name: "folder", kind: 'dir', <fs name: "a.bin", kind: 'file', b'\x00FF00'>>
    >;
    output(tree, "temp/zip-tree.zip", {compression: 'stored', deterministic: true})^
    let written = content(input("temp/zip-tree.zip")^)^
    output(input("test/input/zip/sample.docx", 'binary')^, "temp/zip-snapshot.docx")^
    let captured = content(input("temp/zip-snapshot.docx")^)^
    output("replaced", "temp/zip-snapshot.docx")^;
    [
        len(members) == 7,
        input(json)^.answer == 42,
        input(raw, 'binary')^ == input(content(source)^[2], 'binary')^,
        input([for (m in written where m.name == "hello.txt") m][0], 'text')^ == "hello",
        input([for (m in written where m.name == "data.json") m][0])^.answer == 42,
        len(content([for (m in written where m.name == "empty") m][0])^) == 0,
        (output(source, "temp/zip-copy.zip", {format: 'zip', mode: 'append'}) or "error") == "error",
        (output(<fs kind: 'dir', <fs name: "../escape", kind: 'file', "bad">>, "temp/zip-copy.zip", 'zip') or "error") == "error",
        len(content(input("temp/zip-copy.zip")^)^) == 7,
        input(captured[1])^.answer == 42,
        (output(tree, "temp/zip-copy.zip", {format: 'zip', max_expanded_bytes: 1}) or "error") == "error",
        len(content(input("temp/zip-copy.zip")^)^) == 7
    ]
}
