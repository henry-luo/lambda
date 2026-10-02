// D2.1.5 and S2.2.3: parsing strings must box the value without advancing twice.
let scalar = parse("\"one\"", 'mark')^;
let values = parse("[\"one\", \"two\", \"\"]", 'mark')^;
let attributes = parse("<node label: \"text\">", 'mark')^;
let text_node = parse("<node \"first\" \"second\">", 'mark')^;
let adjacent = parse("<node \"first\"\"second\">", 'mark')^;
let doc = input('./test/input/example.mark', 'mark')^;
let examples = doc[1][1];
[
    scalar == "one", scalar is string,
    values == ["one", "two", ""], values[2] is string,
    attributes.label == "text", attributes.label is string,
    text_node[0] == "firstsecond", adjacent[0] == "firstsecond",
    doc[0][0] == "Welcome to Mark programming language!",
    examples.Elements is element, examples.Elements.attr == 'value',
    examples.Objects is map, examples.Objects.key == 'value',
    examples.Arrays is array, examples.Arrays == ['item1', 'item2'],
    examples.Strings is string, examples.Strings == "text",
    examples.Symbols is symbol, examples.Symbols == 'symbol',
    examples.Numbers == [123, 3.14, 2.71m], examples.Numbers[2] is decimal,
    examples.Dates is datetime,
    examples.Binary is binary, examples.Binary == b'A0FE', len(examples.Binary) == 2
]
