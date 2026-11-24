let zero = "\u0000";
let ascii = zero ++ "a" ++ zero ++ "b" ++ zero;
let unicode = "é" ++ zero ++ "🙂";
let parts = split(ascii, "\u0000");
let diagnostic = error({message: ascii, source: error(unicode)});
let keyed = parse("{\"a\":1,\"a\\u0000b\":2}", 'json')^;
let json_object = parse(format(keyed, 'json')^, 'json')^;
let yaml_object = parse(format(keyed, 'yaml')^, 'yaml')^;
type digit = \("\d");
type zero_pattern = \("\u0000");
all([
    len(ascii) == 5, ord(ascii[0]) == 0, ascii[3] == "b", ascii[4] == zero,
    len(unicode) == 3, ord(unicode[1]) == 0, unicode[2] == "🙂",
    slice(ascii, 1, 4) == "a\u0000b", slice(unicode, 1, 3) == "\u0000🙂",
    contains(ascii, "\u0000b"), index_of(ascii, "\u0000b") == 2,
    index_of(unicode, "🙂") == 2, last_index_of(ascii, zero) == 4,
    parts == ["", "a", "b", ""], join(parts, zero)^ == ascii,
    replace(ascii, zero, "|") == "|a|b|", replace("abc", "b", zero) == "a\u0000c",
    replace("a\u00001", digit, "x") == "a\u0000x",
    find("a\u00001", digit)[0].index == 2,
    split("a\u0000b", zero_pattern) == ["a", "b"],
    upper(ascii) == "\u0000A\u0000B\u0000", lower("É\u0000A") == "é\u0000a",
    trim(" \u0000a\u0000 ") == "\u0000a\u0000", chr(0) == zero,
    [for (c in unicode) ord(c)] == [233, 0, 128578],
    parse(format(unicode, 'json')^, 'json')^ == unicode,
    parse(format(unicode, 'yaml')^, 'yaml')^ == unicode,
    json_object.a == 1, json_object["a\u0000b"] == 2,
    yaml_object.a == 1, yaml_object["a\u0000b"] == 2,
    format(unicode, 'mdx')^ == unicode,
    format(<js unicode>, 'jsx')^ == "{" ++ unicode ++ "}",
    parse(unicode, 'text')^ == unicode,
    (parse("key: a\u0000b", 'yaml') ^ { null }) == null,
    (parse("hello", "text\u0000bad") ^ { null }) == null,
    (format(ascii, "json\u0000bad") ^ { null }) == null,
    all([for (value in [ascii, {inner: {text: ascii}}, <p title: ascii>, <script ascii>, keyed])
        (format(value, 'xml') ^ { ^.code }) == 408]),
    format(<p "ordinary">, 'xml')^ == "<p>ordinary</p>",
    diagnostic.message == ascii, diagnostic.source.message == unicode,
    format(t'2025-04-26', "YYYY\u0000MM")^ == "2025\u000004",
    url_encode("a\u0000b") == "a%00b"
])
