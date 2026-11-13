import f: lambda.editor.format.rtf
import lambda.editor.format.model
import lambda.editor.core.doc
import lambda.editor.core.step

let source = "{\\rtf1\\ansi Hello \\b bold\\b0  world\\par \\u-10179?\\u-8704?}"
let loaded = f.import_text(source)^
let edited = node('doc', [node('p', [text_marked("Changed 😀", [mark('strong', true)])])])
let output = f.export_text(edited, loaded.envelope)^
let back = f.import_text(output)^;
let escaped = f.import_text("{\\rtf1\\ansi Caf\\'e9}") ^ { null };
let blanks = f.import_text("{\\rtf1 hello\\par\\par world\\par\\par}")^
let blank_edit = node('doc', [node('p', []), node('p', [text("Changed")]), node('p', [])])
let blank_output = f.export_text(blank_edit, blanks.envelope)^
let blank_back = f.import_text(blank_output)^;
let punctuation = f.import_text("{\\rtf1 A\\bullet\\emdash\\_B}")^
let punctuation_edit = node('doc', [node('p', [text("A•—‑B edited")])])
let punctuation_output = f.export_text(punctuation_edit, punctuation.envelope)^
let punctuation_back = f.import_text(punctuation_output)^;
let euro = f.import_text("{\\rtf1\\ansi\\ansicpg1252 \\'80}")^;
let font_source = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fnil\\fcharset0 Arial;}{\\f1 Times New Roman;}}\\f0\\fs30 Hello}"
let font = f.import_text(font_source)^
let font_doc = node('doc', [node('p', [text("Hello edited")])])
let font_output = f.export_text(font_doc, font.envelope)^
let font_back = f.import_text(font_output)^;
let empty = f.import_text("{\\rtf1}")^
let view_source = "{\\rtf1\\ansi\\viewkind4\\viewscale125\\viewzk2 \\plain Hello}"
let view_doc = f.import_text(view_source)^
let view_output = f.export_text(node('doc', [node('p', [text("Hello edited")])]), view_doc.envelope)^
let view_back = f.import_text(view_output)^
let color_source = "{\\rtf1\\ansi{\\colortbl;\\red255\\green0\\blue0;\\red0\\green0\\blue255;}Normal \\cf1 red \\b bold\\b0  \\cf2 blue\\cf0  end}"
let color_doc = f.import_text(color_source)^
let color_output = f.export_text(node('doc', [node('p', [*color_doc.doc.content[0].content, text("!")])]), color_doc.envelope)^
let color_back = f.import_text(color_output)^;
[
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  f.check_roundtrip(loaded.doc, loaded.envelope) == null,
  f.check_roundtrip(edited, loaded.envelope) == null,
  join([for (part in back.doc.content[0].content) part.text], "") == "Changed 😀",
  back.doc.content[0].content[0].marks[0].name == 'strong',
  (f.import_text("{\\rtf1{\\fonttbl font;}hello}") ^ { null }) == null,
  len(blanks.doc.content) == 4 and len(blanks.doc.content[1].content) == 0 and len(blanks.doc.content[3].content) == 0,
  (f.import_text("{\\rtf1 \\u-10179?}") ^ { null }) == null,
  (f.import_text("{\\rtf1 hello\\unknown world}") ^ { null }) == null,
  escaped != null and escaped.doc.content[0].content[0].text == "Café",
  escaped != null and f.check_roundtrip(escaped.doc, escaped.envelope) == null,
  euro.doc.content[0].content[0].text == "€",
  (f.import_text("{\\rtf1\\ansi\\ansicpg1251 \\'80}") ^ { null }) == null,
  (f.import_text("\\'e9{\\rtf1\\ansi text}") ^ { null }) == null,
  f.check_roundtrip(blank_edit, blanks.envelope) == null,
  len(blank_back.doc.content) == 3 and len(blank_back.doc.content[0].content) == 0 and len(blank_back.doc.content[2].content) == 0,
  f.check_roundtrip(punctuation_edit, punctuation.envelope) == null,
  join([for (part in punctuation_back.doc.content[0].content) part.text], "") == "A•—‑B edited",
  f.export_text(font.doc, font.envelope)^ == font_source,
  font.envelope.font_style == "font-family:'Arial';font-size:20.00px;",
  font_back.envelope.font_style == font.envelope.font_style,
  font_back.doc.content[0].content[0].text == "Hello edited",
  contains(font_output, "{\\fonttbl{\\f0\\fnil\\fcharset0 Arial;}{\\f1 Times New Roman;}}"),
  f.check_roundtrip(font_doc, font.envelope) == null,
  (f.import_text("{\\rtf1{\\fonttbl{\\f0\\fcharset204 Arial;}} A}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\fonttbl{\\f0 Arial;}} A {\\f1 B}}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\fonttbl{\\f0 Arial;}} A\\plain B}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\fonttbl{\\f0\\bin2 ab;}} A}") ^ { null }) == null,
  len(empty.doc.content) == 1 and len(empty.doc.content[0].content) == 0,
  f.check_roundtrip(empty.doc, empty.envelope) == null,
  f.export_text(empty.doc, empty.envelope)^ == "{\\rtf1}",
  f.export_text(view_doc.doc, view_doc.envelope)^ == view_source,
  contains(view_output, "\\viewkind4 ") and contains(view_output, "\\viewscale125 ") and contains(view_output, "\\viewzk2 "),
  view_back.doc.content[0].content[0].text == "Hello edited",
  (f.import_text("{\\rtf1 hello \\viewscale100}") ^ { null }) == null,
  (f.import_text("{\\rtf1\\viewscale hello}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\viewkind4 }hello}") ^ { null }) == null,
  f.export_text(color_doc.doc, color_doc.envelope)^ == color_source,
  contains(color_output, "{\\colortbl;\\red255\\green0\\blue0;\\red0\\green0\\blue255;}") and contains(color_output, "\\cf1") and contains(color_output, "\\cf2"),
  f.check_roundtrip(color_doc.doc, color_doc.envelope) == null,
  join([for (part in color_back.doc.content[0].content) part.text], "") == "Normal red bold blue end!",
  (f.import_text("{\\rtf1{\\colortbl;\\red256;}\\cf1 bad}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\colortbl;\\red1}x}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\colortbl;}\\cf2 bad}") ^ { null }) == null,
  f.check_roundtrip(node('doc', [node('p', [text_marked("bad", [mark('span', [{name:'style', value:"color: rgb(255,0,0); background-color: blue"}])])])]), color_doc.envelope) != null
]
