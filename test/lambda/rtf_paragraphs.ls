import rtf: lambda.doc.rtf
let source = "{\\rtf1\\par one\\par\\par two\\par\\par}"
let rich = rtf.parse(source, {preserve_empty:true})^
let grammar_doc = rtf.parse_grammar(source, {preserve_empty:true})^
let legacy = rtf.parse(source)^
let blocks = content(rich)^;
[
  len(blocks) == 5,
  len(content(blocks[0])^) == 0,
  len(content(blocks[2])^) == 0,
  len(content(blocks[4])^) == 0,
  format(rich, 'json')^ == format(grammar_doc, 'json')^,
  len(content(legacy)^) == 2
]
