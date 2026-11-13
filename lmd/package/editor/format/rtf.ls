// Rich RTF subset. Unknown controls and destinations open in source mode;
// every accepted edit must survive the package reader's model round trip.
import rtf: lambda.doc.rtf
import lex: lambda.doc.rtf.lexer
import html: lambda.editor.format.html
import lambda.editor.format.model
import lambda.editor.core.doc
import lambda.editor.core.step
import inline_format: lambda.editor.core.inline_format

let supported = ["rtf", "ansi", "ansicpg", "uc", "u", "par", "line", "tab", "b", "i", "ul", "ulnone", "strike", "plain", "pard", "deff", "f", "fs", "cf", "viewkind", "viewscale", "viewzk"]

// Only ordinary font entries with the ANSI charset are understood. The raw
// table is retained, while embedded fonts and alternate encodings stay source-only.
fn font_table_end(source, at) int^ {
  var cursor = at, depth = 1, entry = false, found = false
  while (depth > 0 and cursor < len(source)) {
    let t = lex.next(source, cursor)^
    if (t == null) { raise error("incomplete RTF font table") }
    if (not (contains(["open", "close", "control", "text"], t.kind) ^ { false })) {
      raise error("this RTF font metadata requires source mode")
    }
    if (t.kind == "open") {
      if (entry) { raise error("nested RTF font entry requires source mode") }
      depth = depth + 1
    }
    if (t.kind == "close") {
      if (entry) { raise error("incomplete RTF font entry") }
      depth = depth - 1
    }
    if (t.kind == "control") {
      if (t.word == "f" and t.parameter != null and t.parameter >= 0 and not entry) { entry = true; found = true }
      else if (not (entry and ((t.word == "fcharset" and t.parameter == 0) or
          (t.word == "fprq" and t.parameter != null and t.parameter >= 0 and t.parameter <= 2) or
          (t.parameter == null and (contains(["fnil", "froman", "fswiss", "fmodern", "fscript", "fdecor", "fbidi"], t.word) ^ { false }))))) {
        raise error("this RTF font metadata requires source mode")
      }
    }
    if (t.kind == "text") {
      let semicolon = index_of(t.value, ";")^
      if (entry and semicolon != null) {
        if (trim(slice(t.value, semicolon + 1, len(t.value))^) != "") { raise error("invalid RTF font entry") }
        entry = false
      }
      else if (not entry and trim(t.value) != "") { raise error("invalid RTF font table") }
    }
    cursor = t.end
  }
  if (depth != 0 or entry or not found) raise error("incomplete RTF font table") else cursor
}

fn group_first(source, at) any^ {
  let t = lex.next(source, at)^
  if (t != null and t.kind == "text" and trim(t.value) == "") lex.next(source, t.end)^ else t
}

// Only RGB entries with explicit terminators can be mapped back to a color.
fn color_table_end(source, at) any^ {
  var cursor = at, count = 0, components = []
  while (cursor < len(source)) {
    let t = lex.next(source, cursor)^
    if (t == null) { raise error("incomplete RTF color table") }
    if (t.kind == "close") {
      if (len(components) != 0 or count == 0) { raise error("incomplete RTF color entry") }
      return {end: t.end, count: count}
    }
    if (t.kind == "control") {
      if (not (t.word in ["red", "green", "blue"]) or t.parameter == null or t.parameter < 0 or t.parameter > 255 or
          (contains(components, t.word) ^ { false })) { raise error("unsupported RTF color metadata") }
      components = [*components, t.word]
    }
    if (t.kind == "text") {
      let chars = split(t.value, "")^
      var i = 0
      while (i < len(chars)) {
        let ch = chars[i]
        if (ch == ";") { count = count + 1; components = [] }
        else if (not (ch in [" ", "\t", "\r", "\n"])) { raise error("invalid RTF color entry") }
        i = i + 1
      }
    }
    if (not (t.kind in ["control", "text"])) { raise error("unsupported RTF color metadata") }
    cursor = t.end
  }
  raise error("incomplete RTF color table")
}

fn validate(source) any^ {
  var cursor = 0, depth = 0, seen = false, closed = false, header = false, body = false, uc = 1, uc_stack = [], pending = null, retained_header = "", has_font = false, colors = 0
  while (cursor < len(source)) {
    let t = lex.next(source, cursor)^
    if (t == null) { return null }
    let is_u = t.kind == "control" and t.word == "u" and t.parameter != null
    let unit = if (is_u) (t.parameter % 65536 + 65536) % 65536 else null
    let low = unit != null and unit >= 56320 and unit <= 57343
    if ((pending != null and not low)^) { raise error("unpaired Unicode escape requires source mode") }
    let safe_hex = t.kind == "hex" and depth > 0 and t.value >= 32 and t.value != 127
    if (not (contains(["open", "close", "control", "text", "symbol"], t.kind) ^ { false }) and not safe_hex or
        (t.kind == "symbol" and (depth <= 0 or not (contains(["\\", "{", "}", "~", "-", "_"], t.value) ^ { false })))) {
      raise error("this RTF token requires source mode")
    }
    if (t.kind == "open") {
      if (depth == 0 and seen) { raise error("multiple RTF roots require source mode") }
      let first = if (depth == 1) group_first(source, t.end)^ else null
      if (first != null and first.kind == "control" and first.word == "fonttbl") {
        if (not header or body) { raise error("this RTF font table requires source mode") }
        let end_pos = font_table_end(source, first.end)^
        retained_header = retained_header ++ slice(source, cursor, end_pos)^
        has_font = true
        cursor = end_pos
      } else if (first != null and first.kind == "control" and first.word == "colortbl") {
        if (not header or body or colors != 0) { raise error("this RTF color table requires source mode") }
        let table = color_table_end(source, first.end)^
        retained_header = retained_header ++ slice(source, cursor, table.end)^
        colors = table.count
        cursor = table.end
      } else {
        depth = depth + 1
        uc_stack = [*uc_stack, uc]
        seen = true
        cursor = t.end
      }
    }
    else if (t.kind == "close") {
      depth = depth - 1
      if (depth < 0) { raise error("unbalanced RTF group") }
      uc = uc_stack[len(uc_stack) - 1]
      uc_stack = take(uc_stack, len(uc_stack) - 1)
      if (depth == 0) { closed = true }
    }
    else if (t.kind == "control") {
      if (depth == 0 or (not (contains(supported, t.word) ^ { false }) and rtf.punctuation[t.word] == null)) {
        raise error("this RTF control requires source mode")
      }
      if (t.word == "u" and not is_u) { raise error("RTF Unicode escape needs a value") }
      if (t.word == "rtf") {
        if (header or depth != 1 or body or t.parameter != 1) { raise error("unsupported RTF header") }
        header = true
      }
      else if (t.word == "ansi") {
        if (depth != 1 or body) { raise error("nested RTF encoding requires source mode") }
      }
      else if (t.word == "ansicpg") {
        if (depth != 1 or body or t.parameter != 1252) { raise error("this RTF code page requires source mode") }
      }
      else if (t.word == "uc") {
        if (t.parameter == null or t.parameter < 0) { raise error("invalid RTF fallback count") }
        uc = t.parameter
      }
      else if (t.word == "cf") {
        if (t.parameter == null or t.parameter < 0 or (t.parameter != 0 and t.parameter >= colors)) {
          raise error("invalid RTF color index")
        }
        body = true
      }
      else if (t.word in ["deff", "f", "fs", "viewkind", "viewscale", "viewzk"]) {
        if (depth != 1 or body or t.parameter == null or t.parameter < 0 or (t.word == "fs" and t.parameter == 0)) {
          raise error("this RTF header control requires source mode")
        }
        // Retain document-wide fonts and view preferences across a rich save.
        retained_header = retained_header ++ "\\" ++ t.word ++ (string(t.parameter)^) ++ " "
        if (t.word in ["deff", "f", "fs"]) { has_font = true }
      }
      else {
        if (t.word == "plain" and has_font) { raise error("this RTF font reset requires source mode") }
        body = true
      }
      if (is_u) {
        if (unit >= 55296 and unit <= 56319) { pending = unit }
        else if (low) {
          if (pending == null) { raise error("unpaired Unicode escape requires source mode") }
          pending = null
        }
        cursor = rtf.fallback(source, t.end, uc)^
      } else { cursor = t.end }
    }
    else if (t.kind == "text") {
      if (depth == 0 and trim(t.value) != "") { raise error("text outside the RTF document") }
      if (depth > 0 and (replace(replace(t.value, "\r", ""), "\n", "") ^ { "" }) != "") { body = true }
      cursor = t.end
    }
    else {
      body = true
      cursor = t.end
    }
    if (t.kind == "close") { cursor = t.end }
  }
  if (depth != 0 or not closed or not header or pending != null) raise error("incomplete RTF document") else retained_header
}

fn span_marks(style) => [
  *if (contains(style, "font-weight:700;") ^ { false }) [mark('strong', true)] else [],
  *if (contains(style, "font-style:italic;") ^ { false }) [mark('em', true)] else [],
  *if (contains(style, "underline") ^ { false }) [mark('u', true)] else [],
  *if (contains(style, "line-through") ^ { false }) [mark('del', true)] else [],
  *if (inline_format.css_get(style, "color") != null)
    [mark('span', [{name: 'style', value: "color: " ++ inline_format.css_get(style, "color")}])] else []
]
fn to_model(parsed) map^ {
  let blocks = [for (block in content(parsed)^)
    node('p', [for (span in content(block)^) text_marked(plain_text(span)^, span_marks(span.style))])];
  // The editable host needs a paragraph even when the source has no text.
  node('doc', if (len(blocks) == 0) [node('p', [])] else blocks)
}
fn unit(n: int) string => "\\u" ++ string(if (n >= 32768) n - 65536 else n) ++ "?"
fn encoded_char(c) any^ {
  let code = ord(c)
  if (c == "\\" or c == "{" or c == "}") "\\" ++ c
  else if (c == "\n") "\\line "
  else if (c == "\t") "\\tab "
  else if (code < 32) raise error("RTF cannot represent this control character")
  else if (code < 127) c
  else if (code <= 65535) unit(code)
  else unit(55296 + (code - 65536) div 1024) ++ unit(56320 + (code - 65536) % 1024)
}
fn encoded(text) any^ => join([for (c in split(text, "")^) encoded_char(c)^], "")
fn mark_control(mark_name) =>
  if (mark_name == 'strong' or mark_name == 'b') "\\b"
  else if (mark_name == 'em' or mark_name == 'i') "\\i"
  else if (mark_name == 'u') "\\ul"
  else if (mark_name == 'del' or mark_name == 's') "\\strike"
  else null
fn color_value(color) => if (not color.specified) null else
  "rgb(" ++ join([for (v in [color.red, color.green, color.blue]) string(v)], ",") ++ ")"
fn control_for_mark(m, colors) any^ {
  if (m.name != 'span') mark_control(m.name)
  else {
    let css = inline_format.span_css([m])^
    let color = inline_format.css_get(css, "color")
    let matches = [for (i, entry in colors where ((color_value(entry) == color and color != null) ^ { false })) i]
    if (not (m.value is array) or len(m.value) != 1 or m.value[0].name != 'style' or
        inline_format.css_set(css, "color", null) != "" or len(matches) == 0) null
    else "\\cf" ++ string(matches[0])
  }
}
fn inline(item, colors) any^ {
  if (is_text(item)) {
    let controls = [for (m in item.marks) control_for_mark(m, colors)^]
    if (any([for (c in controls) c == null])) { raise error("RTF cannot represent a text style") }
    else if (len(controls) == 0) encoded(item.text)^
    else "{" ++ join(controls, "") ++ " " ++ encoded(item.text)^ ++ "}"
  }
  else if (is_node(item) and item.tag == 'br') "\\line "
  else if (is_node(item) and mark_control(item.tag) != null)
    "{" ++ mark_control(item.tag) ++ " " ++ join([for (child in item.content) inline(child, colors)^], "") ++ "}"
  else raise error("RTF cannot represent this inline content")
}
fn write_document(doc, envelope) string^ {
  let bad = [for (block in doc.content where (not (is_node(block) ^ { false }) or block.tag != 'p')) block]
  if (len(bad) > 0) { raise error("RTF cannot represent this block") }
  else {
    let paragraphs = [for (block in doc.content) join([for (child in block.content) inline(child, envelope.colors)^], "")]
    // An empty final block needs its own terminator; a final nonempty one does not.
    let terminal = if (len(paragraphs) > 0 and paragraphs[len(paragraphs) - 1] == "") "\\par " else ""
    "{\\rtf1\\ansi\\uc1 " ++ envelope.retained_header ++ join(paragraphs, "\\par ") ++ terminal ++ "}"
  }
}
pub fn import_text(source) map^ {
  let retained_header = validate(source)^
  let parsed = rtf.parse_details(source, {preserve_empty: true})^
  let doc = to_model(parsed.document)^;
  {doc: doc, envelope: {source: source, original: doc, retained_header: retained_header, font_style: parsed.font_style, colors: parsed.colors}}
}
pub fn export_text(doc, envelope) any^ =>
  if (doc == envelope.original) envelope.source else (write_document(doc, envelope) ^ { null })
pub fn check_roundtrip(doc, envelope) any^ {
  let written = write_document(doc, envelope) ^ { null }
  if (written == null) "this RTF edit is not representable"
  else {
    let parsed = rtf.parse(written, {preserve_empty: true}) ^ { null }
    let back = if (parsed == null) null else (to_model(parsed) ^ { null })
    if (back == null or html.normal_form(back) != html.normal_form(doc))
      "this RTF edit cannot round-trip" else null
  }
}
pub let descriptor = {id: 'rtf', name: "RTF", suffixes: [".rtf"], surface: 'rich_text', fallback_source: true,
  schema: html.schema, schema_preset: 'html5_subset', underline: true, view_only_tags: [],
  unsupported_input_types: ["formatBlock", "insertUnorderedList", "insertOrderedList", "formatIndent", "formatOutdent",
    "formatBlockquote", "formatLiftBlockquote", "insertLink", "insertImage", "insertCodeBlock", "insertHorizontalRule",
    "insertTable", "insertTableRow", "deleteTableRow", "insertTableColumn", "deleteTableColumn", "modelToggleMark"],
  allowed_model_marks: ['del'],
  surface_style: (envelope) => envelope.font_style,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip}
