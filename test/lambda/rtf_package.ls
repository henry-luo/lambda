import rtf: lambda.doc.rtf
let cases = [
 {name:"basic",source:"{\\rtf1 Hello {\\b bold {\\i both} bold} end\\par}"},
 {name:"tables",source:"{\\rtf1\\deff1{\\fonttbl{\\f0 Times New Roman;}{\\f1 Arial;}}{\\colortbl;\\red1\\green22\\blue255;}\\qc\\li120\\ri30\\sl240\\f1\\fs28\\cf1 hi}"},
 {name:"escapes",source:"{\\rtf1 A\\{B\\}\\\\ \\'e9 \\~ \\- \\bullet \\tab \\line X\\par}"},
 {name:"unicode",source:"{\\rtf1\\uc1 \\u233?\\uc2 \\u-10179ab \\u945\\'3fX}"},
 {name:"destinations",source:"{\\rtf1 A{\\*\\unknown hidden {nested}}{\\info secret}{\\pict data}B\\page C}"},
 {name:"reset",source:"{\\rtf1\\b\\i\\ul\\strike\\caps\\super X\\plain Y\\ulnone Z\\pard\\qj Q}"},
 {name:"empty-color-slots",source:"{\\rtf1{\\colortbl;;;\\red300\\green-4\\blue12;}\\cf3 color}"},
 {name:"unterminated",source:"{\\rtf1 A {\\i B"},
 {name:"utf8-fallback",source:"{\\rtf1 é 漢字 \\uc2 \\u233\\'3f?x}"},
 {name:"trailing-group",source:"{\\rtf1 a}{\\rtf1 ignored}"},
 {name:"tablesep",source:"{\\rtf1 a\\cell b\\row c\\par}"}
];
let oracle = input("test/input/rtf/native_oracles.json",'json')^;
[for (i,c in cases, let a = format(rtf.parse(c.source)^,'json')^) {name:c.name,equal:c.name == oracle.cases[i].name and a == oracle.cases[i].expected,grammar:format(rtf.parse_grammar(c.source)^,'json')^ == a}]
let fixture = input("test/input/test.rtf",'text')^;
{fixture:format(rtf.parse(fixture)^,'json')^ == oracle.fixture,
 grammar_fixture:format(rtf.parse_grammar(fixture)^,'json')^ == format(rtf.parse(fixture)^,'json')^,
 invalid:(rtf.parse("not rtf") ^ { true }),grammar_invalid:(rtf.parse_grammar("not rtf") ^ { true })}

let nested = join([for (i in 1 to 512) "{"], "") ++ "x" ++ join([for (i in 1 to 512) "}"], "");
let too_deep = rtf.parse("{\\rtf1 " ++ nested ++ "}");
{depth_guard:too_deep is error,grammar_depth_guard:rtf.parse_grammar("{\\rtf1 " ++ nested ++ "}") is error}
let within = join([for (i in 1 to 511) "{"], "") ++ "x" ++ join([for (i in 1 to 511) "}"], "");
let deep_source = "{\\rtf1 " ++ within ++ "}";
{depth_511:format(rtf.parse(deep_source)^,'json')^ == oracle.depth_511,
 grammar_depth_511:format(rtf.parse_grammar(deep_source)^,'json')^ == format(rtf.parse(deep_source)^,'json')^}

let skip_source = "{\\rtf1 A{\\*\\unknown " ++
    join([for (i in 1 to 1500) "{"], "") ++ "x" ++
    join([for (i in 1 to 1500) "}"], "") ++ "}B}";
{deep_skipped_destination:format(rtf.parse(skip_source)^,'json')^ == oracle.deep_skipped_destination,
 grammar_deep_skipped_destination:format(rtf.parse_grammar(skip_source)^,'json')^ == format(rtf.parse(skip_source)^,'json')^}
// Preserve the former native robustness cases after moving parsing to the package.
{oracle_cases_complete:len(oracle.cases) == len(cases),
 minimal:all([for (source in ["{\\rtf1}","{\\rtf1 Hello}","{\\rtf1\\ansi}"])
    rtf.parse(source)^ is element and rtf.parse_grammar(source)^ is element]),
 nested:all([for (depth in [10,100,200,256]) {
    let body = join([for (i in 1 to depth) "{"], "") ++ "text" ++
        join([for (i in 1 to depth) "}"], "");
    let source = "{\\rtf1 " ++ body ++ "}";
    format(rtf.parse(source)^,'json')^ == format(rtf.parse_grammar(source)^,'json')^
 }]),
 empty_invalid:(rtf.parse("") ^ { true }) and (rtf.parse_grammar("") ^ { true })}
// MIME detection still identifies RTF, but generic input leaves it as text.
let legacy_rtf = parse("{\\rtf1 text}",'rtf');
{legacy_dispatch_removed:legacy_rtf is error,auto_source_is_text:input("test/input/test.rtf")^ == fixture}
