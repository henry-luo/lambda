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
let color_back = f.import_text(color_output)^
let script_source = "{\\rtf1\\ansi H{\\sub 2}O and x{\\super 2} plain}"
let script_doc = f.import_text(script_source)^
let script_edit = node('doc', [node('p', [*script_doc.doc.content[0].content, text("!")])])
let script_output = f.export_text(script_edit, script_doc.envelope)^
let script_back = f.import_text(script_output)^
let runs_source = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}{\\f1 Times New Roman;}}\\f0\\fs24 Plain {\\f1\\fs36 Serif} plain}"
let runs = f.import_text(runs_source)^
let runs_edit = node('doc', [node('p', [*runs.doc.content[0].content, text("!")])])
let runs_output = f.export_text(runs_edit, runs.envelope)^
let runs_back = f.import_text(runs_output)^
let ungrouped_source = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}{\\f1 Times New Roman;}}\\f0\\fs24 First \\f1\\fs36 Last}"
let ungrouped = f.import_text(ungrouped_source)^
let ungrouped_edit = node('doc', [node('p', [*ungrouped.doc.content[0].content, text("!")])])
let ungrouped_output = f.export_text(ungrouped_edit, ungrouped.envelope)^
let mixed_source = "{\\rtf1{\\fonttbl{\\f0 Arial;}{\\f1 Courier New;}}{\\colortbl;\\red255\\green0\\blue0;}a {\\f1\\fs30\\cf1\\super mixed} b}"
let mixed = f.import_text(mixed_source)^
let mixed_edit = node('doc', [node('p', [*mixed.doc.content[0].content, text("!")])])
let mixed_output = f.export_text(mixed_edit, mixed.envelope)^
let reset_source = "{\\rtf1\\deff0{\\fonttbl{\\f0 Arial;}{\\f1 Courier New;}}{\\colortbl;\\red255\\green0\\blue0;}\\f1\\fs36\\b\\cf1 Bold \\plain reset}"
let reset_doc = f.import_text(reset_source)^
let reset_edit = node('doc', [node('p', [*reset_doc.doc.content[0].content, text("!")])])
let reset_output = f.export_text(reset_edit, reset_doc.envelope)^
let reset_back = f.import_text(reset_output)^;
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
  (f.import_text("{\\rtf1{\\fonttbl{\\f0 Arial;}} A\\plain B}") ^ { null }) != null,
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
  f.check_roundtrip(node('doc', [node('p', [text_marked("bad", [mark('span', [{name:'style', value:"color: rgb(255,0,0); background-color: blue"}])])])]), color_doc.envelope) != null,
  f.export_text(script_doc.doc, script_doc.envelope)^ == script_source,
  f.check_roundtrip(script_edit, script_doc.envelope) == null,
  contains(script_output, "\\sub ") and contains(script_output, "\\super "),
  any([for (part in script_back.doc.content[0].content) part.text == "2" and any([for (m in part.marks) m.name == 'sub'])]),
  any([for (part in script_back.doc.content[0].content) part.text == "2" and any([for (m in part.marks) m.name == 'sup'])]),
  (f.import_text("{\\rtf1 x\\super0 y}") ^ { null }) == null,
  f.export_text(runs.doc, runs.envelope)^ == runs_source,
  f.check_roundtrip(runs_edit, runs.envelope) == null,
  contains(runs_output, "\\f1\\fs36") and contains(runs_output, "Serif"),
  any([for (part in runs_back.doc.content[0].content) part.text == "Serif" and any([for (m in part.marks) m.name == 'span'])]),
  (f.import_text("{\\rtf1{\\fonttbl{\\f0 A;}{\\f0 B;}} text}") ^ { null }) == null,
  (f.import_text("{\\rtf1{\\fonttbl{\\f0 A;}} text {\\f3 bad}}") ^ { null }) == null,
  ungrouped.envelope.base_font == 0 and ungrouped.envelope.base_size == 24,
  f.check_roundtrip(ungrouped_edit, ungrouped.envelope) == null and contains(ungrouped_output, "\\f1\\fs36"),
  (f.import_text("{\\rtf1\\deff3{\\fonttbl{\\f0 A;}} text}") ^ { null }) == null,
  f.check_roundtrip(mixed_edit, mixed.envelope) == null,
  contains(mixed_output, "\\super\\f1\\fs30\\cf1"),
  f.export_text(reset_doc.doc, reset_doc.envelope)^ == reset_source,
  f.check_roundtrip(reset_edit, reset_doc.envelope) == null,
  contains(reset_output, "\\f0\\fs24") and contains(reset_output, "reset"),
  join([for (part in reset_back.doc.content[0].content) part.text], "") == "Bold reset!",
  (f.import_text("{\\rtf1 text\\plain1 invalid}") ^ { null }) == null
]
