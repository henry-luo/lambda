import t: lambda.editor.format.toml
let text = "# comment\r\n[ server ] # main\r\nport = 80\r\n[server.tls]\r\nport = 443\r\n[[server.backends]]\r\nname = 'one'\r\n[[server.backends]]\r\nname = 'two'\r\n[other]\r\nvalue = 1\r\n"
let loaded = t.import_text(text)^
let section = loaded.doc.tree.children[0]
let changed = t.rename_key(loaded.doc, section.first, section.key_end, "\"new.server\"")^
let parsed = parse(changed.doc.text, 'toml')^
let duplicate = t.rename_key(loaded.doc, section.first, section.key_end, "other") ^ { null }
let malformed = t.rename_key(loaded.doc, section.first, section.key_end, "new.\n[injected]") ^ { null }
let unchanged = t.rename_key(loaded.doc, section.first, section.key_end, "server")^;
[
  section.name == "server",
  contains(changed.doc.text, "[ \"new.server\" ] # main\r\n"),
  contains(changed.doc.text, "[\"new.server\".tls]\r\n"),
  contains(changed.doc.text, "[[\"new.server\".backends]]\r\n"),
  parsed["new.server"].port == 80 and parsed["new.server"].tls.port == 443,
  len(parsed["new.server"].backends) == 2 and parsed["new.server"].backends[1].name == "two",
  parsed.other.value == 1,
  duplicate == null,
  malformed == null,
  unchanged.doc.text == text
]
