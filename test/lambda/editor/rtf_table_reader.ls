import rtf: lambda.doc.rtf
import lambda.editor.format.model

let options = {preserve_empty:true, editor_runs:true, editor_tables:true}
let source = "{\\rtf1 Before\\par \\trowd\\cellx1440\\cellx3240\\intbl A\\cell {\\b B}\\cell\\row \\pard After\\par}"
let direct = rtf.parse(source, options)^
let grammatical = rtf.parse_grammar(source, options)^
let implied_runs = rtf.parse(source, {preserve_empty:true, editor_tables:true})^
let parts = content(direct)^
let cells = content(parts[1])^
let blank = rtf.parse("{\\rtf1\\trowd\\cellx1000\\cellx2200\\intbl\\cell X\\cell\\row}", options)^
let blank_parts = content(blank)^
let blank_cells = content(blank_parts[0])^
let plain = rtf.parse(source)^
let plain_parts = content(plain)^
let invalid = [
  "{\\rtf1\\trowd\\cellx1000\\cellx900\\intbl X\\cell\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000\\intbl X\\cell}",
  "{\\rtf1\\trowd\\cellx1000\\cellx2000\\intbl X\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000\\intbl X\\par Y\\cell\\row}"
];
[
  [for (part in parts) name(part)] == ['p', 'rtf-row', 'p'],
  [for (cell in cells) cell.rtf_width] == [1440, 1800],
  [for (cell in cells) plain_text(cell)] == ["A", "B"],
  contains(cells[1][0].style, "font-weight:700"),
  [for (cell in blank_cells) cell.rtf_width] == [1000, 1200],
  [for (cell in blank_cells) plain_text(cell)] == ["", "X"],
  format(direct, 'json')^ == format(grammatical, 'json')^,
  format(direct, 'json')^ == format(implied_runs, 'json')^,
  all([for (s in invalid) (rtf.parse(s, options) ^ { null }) == null]),
  name(plain_parts[1]) == 'p'
]
