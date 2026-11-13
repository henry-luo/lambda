import xml: lambda.editor.format.xml
let source = "<?xml version=\"1.0\"?>\n<r id=\"a&#38;b\"><x>hello &amp; world</x><!--keep--><z>\nmulti\n</z><![CDATA[raw <&>\nline]]></r>\n"
let loaded = xml.import_text(source)^
let root = loaded.doc.tree.children[0].node
let attr = root.children[0].node
let text_node = root.children[1].node.children[0].node
let multiline = root.children[2].node.children[0].node
let cdata = root.children[3].node
let unchanged = xml.replace_scalar(loaded.doc, attr.first, attr.last, attr.text)^
let changed = xml.replace_scalar(loaded.doc, attr.first, attr.last, "a&\"b<")^
let text_changed = xml.replace_scalar(loaded.doc, text_node.first, text_node.last, "new < & >")^
let multi_changed = xml.replace_scalar(loaded.doc, multiline.first, multiline.last, "one\ntwo")^
let cdata_changed = xml.replace_scalar(loaded.doc, cdata.first, cdata.last, "raw <new>&\nline")^
let renamed = xml.rename_key(loaded.doc, attr.key_first, attr.key_end, "@code")^
let added = xml.insert_child(loaded.doc, root.first, "title", "a&\"b\n")^
let deleted = xml.delete_child(loaded.doc, attr.first)^;
let normalized_source = "<r a='x&#xA;&apos;&#x9;y'>A\r\nB<![CDATA[C\r\nD]]></r>"
let normalized = xml.import_text(normalized_source)^
let normalized_root = normalized.doc.tree.children[0].node
let normalized_attr = normalized_root.children[0].node
let normalized_text = normalized_root.children[1].node
let normalized_cdata = normalized_root.children[2].node
let normalized_unchanged = xml.replace_scalar(normalized.doc, normalized_cdata.first, normalized_cdata.last, normalized_cdata.text)^
let single_quote = xml.replace_scalar(normalized.doc, normalized_attr.first, normalized_attr.last, "O'Brien\tline\r\n")^;
let empty_source = "<r><x></x><y a=\"v\"> \r\n </y><z/></r>"
let empty = xml.import_text(empty_source)^
let empty_root = empty.doc.tree.children[0].node
let empty_text = empty_root.children[0].node.children[0].node
let whitespace_text = empty_root.children[1].node.children[1].node
let self_closing_text = empty_root.children[2].node.children[0].node
let filled = xml.replace_scalar(empty.doc, empty_text.first, empty_text.last, "new & value")^
let self_unchanged = xml.replace_scalar(empty.doc, self_closing_text.first, self_closing_text.last, "")^
let self_filled = xml.replace_scalar(empty.doc, self_closing_text.first, self_closing_text.last, "<new>")^;
let unicode_source = "<r xmlns:n=\"urn:x\"><n:item id='😀' /></r>"
let unicode = xml.import_text(unicode_source)^
let unicode_item = unicode.doc.tree.children[0].node.children[1].node
let unicode_text = unicode_item.children[1].node
let unicode_filled = xml.replace_scalar(unicode.doc, unicode_text.first, unicode_text.last, "雪 & 😀")^;
let added_element = xml.insert_element(loaded.doc, root.first, "new", "A & <B>")^
let empty_element = xml.insert_element(loaded.doc, root.first, "empty", "")^
let expanded_parent = xml.insert_element(unicode.doc, unicode_item.first, "n:child", "雪 & 😀")^
let deleted_element = xml.delete_element(loaded.doc, root.children[1].node.first)^
let renamed_element = xml.rename_key(loaded.doc, root.children[1].first, root.children[1].key_end, "entry")^
let renamed_root = xml.rename_key(loaded.doc, root.first, root.insert_at, "document")^
let renamed_empty = xml.rename_key(unicode.doc, unicode_item.first, unicode_item.insert_at, "n:entry")^;
[
  attr.text == "a&b",
  text_node.text == "hello & world",
  multiline.multiline and multiline.text == "\nmulti\n",
  cdata.multiline and cdata.text == "raw <&>\nline",
  unchanged.doc.text == source,
  contains(changed.doc.text, "id=\"a&amp;&quot;b&lt;\""),
  contains(text_changed.doc.text, "<x>new &lt; &amp; &gt;</x>"),
  contains(multi_changed.doc.text, "<z>one\ntwo</z>"),
  contains(cdata_changed.doc.text, "<![CDATA[raw <new>&\nline]]>"),
  contains(renamed.doc.text, "code=\"a&#38;b\""),
  contains(added.doc.text, "title=\"a&amp;&quot;b&#xA;\""),
  contains(deleted.doc.text, "<r ><x>"),
  (xml.import_text("<r><x></r>") ^ { null }) == null,
  (xml.insert_child(loaded.doc, root.first, "id", "duplicate") ^ { null }) == null,
  (xml.insert_child(loaded.doc, root.first, "bad name", "value") ^ { null }) == null,
  (xml.insert_child(loaded.doc, root.first, "new=\"x\" other", "value") ^ { null }) == null,
  (xml.rename_key(loaded.doc, attr.key_first, attr.key_end, "new=\"x\" other") ^ { null }) == null,
  (xml.replace_scalar(loaded.doc, cdata.first, cdata.last, "bad]]>value") ^ { null }) == null,
  (xml.replace_scalar(loaded.doc, cdata.first, cdata.last, "bad\rvalue") ^ { null }) == null,
  (xml.replace_scalar(loaded.doc, cdata.first, cdata.last, chr(1)) ^ { null }) == null,
  (xml.replace_scalar(loaded.doc, text_node.first, text_node.last, chr(1)) ^ { null }) == null,
  (xml.delete_child(loaded.doc, text_node.first) ^ { null }) == null,
  (xml.replace_scalar(loaded.doc, attr.first, attr.last + 1, "bad") ^ { null }) == null,
  normalized_attr.text == "x\n'\ty" and normalized_text.text == "A\nB" and normalized_cdata.text == "C\nD",
  normalized_unchanged.doc.text == normalized_source,
  contains(single_quote.doc.text, "a='O&apos;Brien&#x9;line&#xD;&#xA;'"),
  empty_text.text == "" and whitespace_text.text == " \n " and whitespace_text.multiline,
  contains(filled.doc.text, "<x>new &amp; value</x>"),
  self_unchanged.doc.text == empty_source,
  contains(self_filled.doc.text, "<z>&lt;new&gt;</z>"),
  unicode_filled.doc.text == "<r xmlns:n=\"urn:x\"><n:item id='😀' >雪 &amp; 😀</n:item></r>",
  contains(added_element.doc.text, "<![CDATA[raw <&>\nline]]><new>A &amp; &lt;B&gt;</new></r>"),
  contains(empty_element.doc.text, "<empty/></r>"),
  expanded_parent.doc.text == "<r xmlns:n=\"urn:x\"><n:item id='😀' ><n:child>雪 &amp; 😀</n:child></n:item></r>",
  contains(deleted_element.doc.text, "<r id=\"a&#38;b\"><!--keep--><z>"),
  (xml.delete_element(loaded.doc, root.first) ^ { null }) == null,
  (xml.delete_element(loaded.doc, attr.first) ^ { null }) == null,
  (xml.insert_element(loaded.doc, attr.first, "new", "") ^ { null }) == null,
  (xml.insert_element(loaded.doc, root.first, "bad name", "") ^ { null }) == null,
  (xml.insert_element(loaded.doc, root.first, "x/><evil", "") ^ { null }) == null,
  (xml.insert_element(loaded.doc, root.first, "missing:child", "") ^ { null }) == null,
  contains(renamed_element.doc.text, "<entry>hello &amp; world</entry><!--keep-->"),
  contains(renamed_root.doc.text, "<document id=\"a&#38;b\">") and ends_with(renamed_root.doc.text, "</document>\n"),
  renamed_empty.doc.text == "<r xmlns:n=\"urn:x\"><n:entry id='😀' /></r>",
  (xml.rename_key(loaded.doc, root.first, root.insert_at, "missing:root") ^ { null }) == null,
  (xml.rename_key(loaded.doc, root.first, root.insert_at, "bad name") ^ { null }) == null,
  (xml.rename_key(loaded.doc, root.first, root.insert_at + 1, "new") ^ { null }) == null
]
