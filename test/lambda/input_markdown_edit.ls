import markdown: lambda.edit.markdown

// Moving definitions to the generated footer must not drop their original source on save.
let sources = [
  "Start[^a].\n\n[^a]: **Note**.\n\nEnd.\n",
  "Start[^q].\n\n> [^q]: Quote note.\n\nEnd.\n",
  "Start[^l].\n\n- item\n\n  [^l]: List note.\n\nEnd.\n"
];
[for (source in sources) {
  let loaded = markdown.import_text(source)^
  markdown.export_text(loaded.doc, loaded.envelope) == source
}]
