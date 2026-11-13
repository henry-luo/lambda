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
 {name:"tablesep",source:"{\\rtf1 a\\cell b\\row c\\par}"}
];
[for (c in cases, let a = format(rtf.parse(c.source)^,'json')^, let b = format(parse(c.source,'rtf')^,'json')^) {name:c.name,equal:a == b}]
let fixture = input("test/input/test.rtf",'text')^;
{fixture:format(rtf.parse(fixture)^,'json')^ == format(parse(fixture,'rtf')^,'json')^,
 invalid:(rtf.parse("not rtf") ^ { true })}

let nested = join([for (i in 1 to 512) "{"], "") ++ "x" ++ join([for (i in 1 to 512) "}"], "");
let too_deep = rtf.parse("{\\rtf1 " ++ nested ++ "}");
{depth_guard:too_deep is error}
let within = join([for (i in 1 to 511) "{"], "") ++ "x" ++ join([for (i in 1 to 511) "}"], "");
let deep_source = "{\\rtf1 " ++ within ++ "}";
{depth_511:format(rtf.parse(deep_source)^,'json')^ == format(parse(deep_source,'rtf')^,'json')^}
