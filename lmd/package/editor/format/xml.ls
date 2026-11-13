// XML property projection. Strict parsing validates each candidate document;
// source spans retain declarations, namespaces, comments and entity spelling.
import model: lambda.editor.format.model

let xml_options = {type: 'xml', flavor: 'strict', preserve_whitespace: true}
fn raw(source: string, first: int, end_pos: int) string => slice(source, first, end_pos) ^ { "" }
fn char_at(source: string, at: int) string => raw(source, at, at + 1)
fn starts_at(source: string, at: int, text: string) bool => raw(source, at, at + len(text)) == text
fn find_from(source: string, at: int, needle: string) int^ {
  let found = index_of(raw(source, at, len(source)), needle)
  if (found == null) raise error("unterminated XML construct") else at + found
}
fn skip_space(source: string, at: int) {
  var i = at
  while (i < len(source) and (contains([" ", "\t", "\r", "\n"], char_at(source, i)) ^ { false })) { i = i + 1 }
  i
}
fn name_end(source: string, at: int) {
  var i = at
  while (i < len(source) and not (contains([" ", "\t", "\r", "\n", "=", "/", ">"], char_at(source, i)) ^ { false })) {
    i = i + 1
  }
  i
}
// Use the XML parser's entity and line-end rules instead of a second decoder.
fn decoded_value(source: string, quote) string^ {
  let needs_parse = contains(source, "&") or contains(source, "\r") or
    (quote != null and (contains(source, "\n") or contains(source, "\t")))
  if (not needs_parse) source else {
    let wrapped = if (quote == null) "<x>" ++ source ++ "</x>" else "<x v=" ++ quote ++ source ++ quote ++ "/>"
    let parsed = parse(wrapped, xml_options)^
    let root = content(parsed)[0];
    if (quote == null) model.plain_text(root)^ else string(root.v or "")
  }
}
fn decoded_cdata(source: string) string^ {
  if (not contains(source, "\r")) source else {
    let parsed = parse("<x><![CDATA[" ++ source ++ "]]></x>", xml_options)^
    model.plain_text(content(parsed)[0])^
  }
}
fn encoded_value(value: string, quote) string^ {
  let escaped = model.escape_text(value)^
  let with_quote = if (quote == "\"") replace(escaped, "\"", "&quot;")^
                   else if (quote == "'") replace(escaped, "'", "&apos;")^ else escaped
  // Literal attribute whitespace is normalized by XML; references retain it.
  let with_tabs = if (quote == null) with_quote else replace(replace(with_quote, "\t", "&#x9;"), "\n", "&#xA;")^
  replace(with_tabs, "\r", "&#xD;")^
}
fn start_tag(source: string, first: int) any^ {
  let tag_end = name_end(source, first + 1)
  let tag = raw(source, first + 1, tag_end)
  var i = skip_space(source, tag_end), attrs = []
  while ((i < len(source) and char_at(source, i) != ">" and not starts_at(source, i, "/>"))^) {
    let end_name = name_end(source, i)
    let key = raw(source, i, end_name)
    let equals = skip_space(source, end_name)
    let quote_at = skip_space(source, equals + 1)
    let quote = char_at(source, quote_at)
    if ((key == "" or char_at(source, equals) != "=" or not (quote == "\"" or quote == "'"))^) {
      raise error("unsupported XML attribute syntax")
    }
    let end_value = find_from(source, quote_at + 1, quote)^
    let attr_text = raw(source, quote_at + 1, end_value)
    let value = decoded_value(attr_text, quote)^
    let multiline = contains(value, "\n") or contains(value, "\r")
    attrs = [*attrs, {name: "@" ++ key, first: i, key_end: end_name,
      node: {kind: 'scalar', first: quote_at + 1, last: end_value, quote: quote, text: value,
             key_first: i, key_end: end_name, attr_last: end_value + 1,
             can_rename: true, can_delete: true, multiline: multiline}}]
    i = skip_space(source, end_value + 1)
  }
  let closed = starts_at(source, i, "/>");
  {tag: tag, children: attrs, insert_at: tag_end, close_at: i, next: i + (if (closed) 2 else 1), closed: closed}
}
fn element_node(opened, first, end_pos, close_at, children, is_root) =>
  {kind: 'object', first: first, last: end_pos, insert_at: opened.insert_at,
   close_at: close_at, close_end: opened.next, tag: opened.tag, self_closing: opened.closed,
   can_rename: true, can_delete: false, can_add_element: true, can_delete_element: not is_root,
   children: children}
fn parse_element(source: string, first: int, is_root = false) any^ {
  let opened = start_tag(source, first)^
  var i = opened.next, children = opened.children
  if (opened.closed) {
    // A text edit expands the empty-element tag while keeping its attributes.
    children = [*children, {name: "#text", first: opened.close_at,
      node: {kind: 'scalar', first: opened.close_at, last: opened.close_at, text: "", mode: 'self_closing',
             tag: opened.tag, close_end: opened.next, can_rename: false, can_delete: false}}]
    return {name: opened.tag, first: first, key_end: opened.insert_at, node: element_node(opened, first, i, opened.close_at, children, is_root), next: i}
  }
  while (i < len(source)) {
    if ((starts_at(source, i, "<!") and not starts_at(source, i, "<!--") and not starts_at(source, i, "<![CDATA["))^) {
      raise error("this XML declaration requires source mode")
    }
    if ((starts_at(source, i, "</"))^) {
      let end_pos = find_from(source, i, ">")^ + 1
      // An explicit empty element still has an insertion point for text.
      if (i == opened.next) {
        children = [*children, {name: "#text", first: i,
          node: {kind: 'scalar', first: i, last: i, quote: null, text: "", can_rename: false, can_delete: false}}]
      }
      return {name: opened.tag, first: first, key_end: opened.insert_at, node: element_node(opened, first, end_pos, i, children, is_root), next: end_pos}
    }
    else if ((starts_at(source, i, "<!--"))^) { i = find_from(source, i + 4, "-->")^ + 3 }
    else if ((starts_at(source, i, "<![CDATA["))^) {
      let first = i + 9
      let end_pos = find_from(source, first, "]]>")^
      let value = decoded_cdata(raw(source, first, end_pos))^
      children = [*children, {name: "#cdata", first: first,
        node: {kind: 'scalar', first: first, last: end_pos, text: value, mode: 'cdata',
               multiline: contains(value, "\n") or contains(value, "\r"), can_rename: false, can_delete: false}}]
      i = end_pos + 3
    }
    else if ((starts_at(source, i, "<?"))^) { i = find_from(source, i + 2, "?>")^ + 2 }
    else if ((char_at(source, i) == "<")^) {
      let child = parse_element(source, i)^
      children = [*children, child]
      i = child.next
    }
    else {
      let end_pos = find_from(source, i, "<")^
      let content = raw(source, i, end_pos)
      // Whitespace is a value when it is the element's only content; between
      // child elements it is structural formatting and stays in the source.
      if (trim(content) != "" or (i == opened.next and starts_at(source, end_pos, "</"))) {
        let value = decoded_value(content, null)^
        let multiline = contains(value, "\n") or contains(value, "\r")
        children = [*children, {name: "#text", first: i,
          node: {kind: 'scalar', first: i, last: end_pos, quote: null, text: value,
                 can_rename: false, can_delete: false, multiline: multiline}}]
      }
      i = end_pos
    }
  }
  raise error("unterminated XML element")
}
pub fn import_text(source: string) map^ {
  let checked = parse(source, xml_options)^
  var i = 0, root = null
  while (i < len(source) and root == null) {
    let next = find_from(source, i, "<")^
    if ((starts_at(source, next, "<!") and not starts_at(source, next, "<!--"))^) {
      raise error("this XML declaration requires source mode")
    }
    if (starts_at(source, next, "<?")) { i = find_from(source, next + 2, "?>")^ + 2 }
    else if (starts_at(source, next, "<!--")) { i = find_from(source, next + 4, "-->")^ + 3 }
    else { root = parse_element(source, next, true)^ }
  }
  if (root == null) { raise error("XML document has no root element") }
  else { {doc: {text: source, tree: {kind: 'object', first: -1, can_add: false, children: [root]}}, envelope: null} }
}
fn find_node(node, first) any^ {
  if (node.first == first) node
  else if (node.kind == 'scalar') null
  else {
    let matches = [for (child in node.children, let found = find_node(child.node, first)^ where found != null) found]
    if (len(matches) == 0) null else matches[0]
  }
}
pub fn replace_scalar(doc, first, end_pos, value) map^ {
  let node = find_node(doc.tree, first)^
  if (node == null or node.kind != 'scalar' or node.last != end_pos) { raise error("XML value no longer exists") }
  else if (value == node.text) { {doc: doc, envelope: null} }
  else if (node.mode == 'cdata' and (contains(value, "]]>") or contains(value, "\r"))^) {
    raise error("CDATA cannot contain ]]> or a carriage return")
  }
  else if (node.mode == 'self_closing') {
    let encoded = encoded_value(value, null)^
    import_text(raw(doc.text, 0, first) ++ ">" ++ encoded ++ "</" ++ node.tag ++ ">" ++
                raw(doc.text, node.close_end, len(doc.text)))^
  }
  else {
    let encoded = if (node.mode == 'cdata') value else encoded_value(value, node.quote)^
    import_text(raw(doc.text, 0, first) ++ encoded ++ raw(doc.text, end_pos, len(doc.text)))^
  }
}
fn attr_name(key: string) string => if (starts_with(key, "@") ^ { false }) (slice(key, 1, len(key)) ^ { "" }) else key
// Validate the token boundary before reparsing, so one name cannot inject
// another attribute into an otherwise well-formed candidate document.
fn valid_xml_name(key: string) bool => key != "" and name_end(key, 0) == len(key) and
  not (contains(key, "\"") ^ { true }) and not (contains(key, "'") ^ { true }) and not (contains(key, "<") ^ { true })
fn replace_span(doc, first, end_pos, value) map^ =>
  import_text(raw(doc.text, 0, first) ++ value ++ raw(doc.text, end_pos, len(doc.text)))^
fn find_attribute(node, key_first) any^ {
  if (node.kind == 'scalar') if (node.key_first == key_first and node.can_rename == true) node else null
  else {
    let matches = [for (child in node.children, let found = find_attribute(child.node, key_first)^ where found != null) found]
    if (len(matches) == 0) null else matches[0]
  }
}
pub fn rename_key(doc, first, end_pos, key) map^ {
  let attr = find_attribute(doc.tree, first)^
  let elem = if (attr == null) find_node(doc.tree, first)^ else null
  if (attr != null and attr.key_end == end_pos) {
    let name = attr_name(key)
    if (not valid_xml_name(name)) raise error("invalid XML attribute name")
    else replace_span(doc, first, end_pos, name)^
  }
  else if (elem != null and elem.tag != null and elem.insert_at == end_pos) {
    if (not valid_xml_name(key)) { raise error("invalid XML element name") }
    else if (elem.self_closing) { replace_span(doc, first + 1, end_pos, key)^ }
    else {
      // Replace both names in one candidate so strict parsing never sees a mismatched pair.
      import_text(raw(doc.text, 0, first + 1) ++ key ++ raw(doc.text, end_pos, elem.close_at + 2) ++
        key ++ raw(doc.text, elem.close_at + 2 + len(elem.tag), len(doc.text)))^
    }
  }
  else { raise error("XML name no longer exists") }
}
pub fn insert_child(doc, first, key, value) map^ {
  let node = find_node(doc.tree, first)^
  let name = attr_name(key)
  if (node == null or node.insert_at == null or not valid_xml_name(name)) { raise error("invalid XML attribute") }
  else replace_span(doc, node.insert_at, node.insert_at, " " ++ name ++ "=\"" ++ encoded_value(value, "\"")^ ++ "\"")^
}
pub fn delete_child(doc, first) map^ {
  let node = find_node(doc.tree, first)^
  if (node == null or node.can_delete != true or node.attr_last == null) { raise error("attribute no longer exists") }
  else replace_span(doc, node.key_first, node.attr_last, "")^
}
pub fn insert_element(doc, first, name: string, value: string) map^ {
  let parent = find_node(doc.tree, first)^
  if (parent == null or parent.can_add_element != true or not valid_xml_name(name)) {
    raise error("invalid XML element")
  }
  else {
    let child = if (value == "") "<" ++ name ++ "/>"
      else "<" ++ name ++ ">" ++ encoded_value(value, null)^ ++ "</" ++ name ++ ">"
    if (parent.self_closing) {
      // Expanding the parent's empty tag keeps its attributes and surrounding source.
      replace_span(doc, parent.close_at, parent.close_end, ">" ++ child ++ "</" ++ parent.tag ++ ">")^
    }
    else replace_span(doc, parent.close_at, parent.close_at, child)^
  }
}
pub fn delete_element(doc, first) map^ {
  let node = find_node(doc.tree, first)^
  if (node == null or node.can_delete_element != true) { raise error("the document root cannot be removed") }
  else replace_span(doc, node.first, node.last, "")^
}
fn export_text(doc, envelope) => doc.text
fn check_roundtrip(doc, envelope) => null
pub let descriptor = {id: 'xml', name: "XML", suffixes: [".xml"], surface: 'property', fallback_source: true,
  multiline_values: true,
  can_add_element: true, can_delete_element: true,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  replace_scalar: replace_scalar, rename_key: rename_key, insert_child: insert_child, delete_child: delete_child,
  insert_element: insert_element, delete_element: delete_element,
  value_help: "Enter the attribute value; XML escaping is automatic."}
