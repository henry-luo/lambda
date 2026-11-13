import rtf: lambda.doc.rtf
import model: lambda.editor.format.model
let source = "{\\rtf1\\ansi A\\bullet\\emdash\\endash\\lquote\\rquote\\ldblquote\\rdblquote\\_B}"
let expected = "A•—–‘’“”‑B";
let windows = "{\\rtf1\\ansi\\ansicpg1252 \\'80 \\'93text\\'94}";
[
  model.plain_text(rtf.parse(source)^)^ == expected,
  model.plain_text(rtf.parse_grammar(source)^)^ == expected,
  model.plain_text(rtf.parse(windows)^)^ == "€ “text”",
  model.plain_text(rtf.parse_grammar(windows)^)^ == "€ “text”"
]
