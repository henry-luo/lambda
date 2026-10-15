import d: lambda.doc.docbook
pn main() {
    let snapshot = d.read("test/input/docbook/media.xml")^
    let result = d.convert(snapshot)^
    let written = d.write(result,"temp/docbook-export/media")^
    let image = input("temp/docbook-export/media/media/1-sample.svg","text")^
    let pure = d.convert(snapshot.document)^
    return [len(result.diagnostics) == 0,written.assets == 4,
        result.assets[3].media_type == "image/svg+xml",contains(image,"Example square") == false,
        contains(image,"viewBox"),pure.diagnostics[0].code == "missing-resource"]
}
