// Dictionary object slots preserve null/empty values and escape PDF syntax.
type Row = {count: int?, wide: i64?, enabled: bool?}
fn dynamic(value) any { value }
fn file_from_map(value) => <file format:'pdf', value>

pn encode(value) string^ {
    output(file_from_map(value), "./temp/pdf_map_objects.txt")^;
    return input("./temp/pdf_map_objects.txt", "text")^
}

pn main() {
    let basic = encode({Type: 'Catalog', Title: "a(b)\\c", Enabled: true,
        Missing: null, Empty: "", Bytes: b'\x00FF', Child: {Count: 2}})^;
    let names = encode({'A/B': 'A/B', 'A#2FB': 'A#2FB', 'a b': 'x%y',
        'é': 'é', *:parse("{\"\": null}", "json")^})^;
    let controls = encode({Text: "\u0000\n\r\t7", Unicode: "é😀"})^;
    let arrays = encode({MediaBox: [0, 0, 595, 842], Mixed: [null, "", 'Page',
        b'\x00FF', {Flag: false}, [1, 2]], Empty: [], Matrix: reshape([1, 2, 3, 4], [2, 2])})^;
    let numbers = encode({Small: -7i8, Wide: 9223372036854775807i64,
        Unsigned: 18446744073709551615u64, Real: 0.1, Tiny: 1e-7,
        Huge: 1e21, Sized: 0.5f32, Plain: -42})^;
    let source = {A: 1, B: 2};
    let spread = encode({*:source, A: 3, C: 4, *:{B: 5, D: 6}, A: 7})^;
    var row: Row = dynamic({count: null, wide: 9223372036854775807i64, enabled: false});
    let typed = encode(row)^;
    let separators = <file format:'pdf', "start" {} {N: 1} "end">;
    output(separators, "./temp/pdf_map_separators.txt")^;
    let separated = input("./temp/pdf_map_separators.txt", "text")^;
    let null_key = (<file format:'pdf', parse("{\"a\\u0000b\": 1}", "json")^>) ^ { "rejected" };
    let null_name = (<file format:'pdf', {Name: 'a\u0000b'}>) ^ { "rejected" };
    let nonfinite = (<file format:'pdf', {Number: inf}>) ^ { "rejected" };
    let nan_value = (<file format:'pdf', {Number: nan}>) ^ { "rejected" };
    let decimal_value = (<file format:'pdf', {Number: decimal(1)}>) ^ { "deferred" };
    let unsupported = (<file format:'pdf', "prefix" {Child: [<node>]}> ) ^ { "rejected" };
    return [
        basic == " << /Type /Catalog /Title (a\\(b\\)\\\\c) /Enabled true /Missing null /Empty () /Bytes <00FF> /Child << /Count 2 >> >> ",
        names == " << /A#2FB /A#2FB /A#232FB /A#232FB /a#20b /x#25y /#C3#A9 /#C3#A9 / null >> ",
        controls == " << /Text (\\000\\012\\015\\0117) /Unicode <FEFF00E9D83DDE00> >> ",
        arrays == " << /MediaBox [0 0 595 842] /Mixed [null () /Page <00FF> << /Flag false >> [1 2]] /Empty [] /Matrix [[1 2] [3 4]] >> ",
        numbers == " << /Small -7 /Wide 9223372036854775807 /Unsigned 18446744073709551615 /Real 0.1 /Tiny 0.0000001 /Huge 1000000000000000000000 /Sized 0.5 /Plain -42 >> ",
        spread == " << /A 7 /B 5 /C 4 /D 6 >> ",
        typed == " << /count null /wide 9223372036854775807 /enabled false >> ",
        separated == "start << >>  << /N 1 >> end",
        null_key, null_name, nonfinite, nan_value, decimal_value, unsupported
    ]
}
