// Rich RTF subset. Unknown controls and destinations open in source mode;
// every accepted edit must survive the package reader's model round trip.
import rtf: lambda.doc.rtf
import lex: lambda.doc.rtf.lexer
import html: lambda.editor.format.html
import lambda.editor.format.model
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.transaction
import lambda.editor.core.source_pos
import commands: lambda.editor.core.commands
import inline_format: lambda.editor.core.inline_format

let header_controls = ["deff", "f", "fs", "viewkind", "viewscale", "viewzk", "deflang", "deflangfe", "adeflang"]
let supported = ["rtf", "ansi", "ansicpg", "uc", "u", "par", "line", "tab", "b", "i", "ul", "ulnone", "strike", "super", "sub", "nosupersub", "plain", "pard", "ql", "qc", "qr", "qj", "li", "ri", "sl", "cf", *header_controls]

// Only ordinary font entries with the ANSI charset are understood. The raw
// table is retained, while embedded fonts and alternate encodings stay source-only.
fn font_table_end(source, at) any^ {
  var cursor = at, depth = 1, entry = false, ids = []
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
      if (t.word == "f" and t.parameter != null and t.parameter >= 0 and not entry) {
        if ((contains(ids, t.parameter))^) { raise error("duplicate RTF font id") }
        entry = true; ids = [*ids, t.parameter]
      }
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
  if (depth != 0 or entry or len(ids) == 0) raise error("incomplete RTF font table") else {end: cursor, ids: ids}
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
  var cursor = 0, depth = 0, seen = false, closed = false, header = false, body = false, paragraph_content = false, uc = 1, uc_stack = [], pending = null, retained_header = "", font_ids = [], colors = 0
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
      let destination = if (first != null and first.kind == "symbol" and first.value == "*")
        group_first(source, first.end)^ else first
      let metadata = destination != null and destination.kind == "control" and
        ((first.kind == "control" and destination.word == "info") or
         (first.kind == "symbol" and destination.word == "generator"))
      if (first != null and first.kind == "control" and first.word == "fonttbl") {
        if (not header or body) { raise error("this RTF font table requires source mode") }
        let table = font_table_end(source, first.end)^
        if (len(font_ids) != 0) { raise error("duplicate RTF font table") }
        retained_header = retained_header ++ slice(source, cursor, table.end)^
        font_ids = table.ids
        cursor = table.end
      } else if (first != null and first.kind == "control" and first.word == "colortbl") {
        if (not header or body or colors != 0) { raise error("this RTF color table requires source mode") }
        let table = color_table_end(source, first.end)^
        retained_header = retained_header ++ slice(source, cursor, table.end)^
        colors = table.count
        cursor = table.end
      } else if (metadata) {
        if (not header or body) { raise error("this RTF metadata requires source mode") }
        let end_pos = rtf.skip_group(source, destination.end, 0, true)^
        // Preserve the Unicode fallback count used by the original metadata.
        retained_header = retained_header ++ "\\uc" ++ string(uc)^ ++ " " ++ slice(source, cursor, end_pos)^ ++ "\\uc1 "
        cursor = end_pos
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
      if (t.word in ["super", "sub", "nosupersub", "plain", "pard", "ql", "qc", "qr", "qj"] and t.parameter != null) {
        raise error("parameterized RTF formatting control requires source mode")
      }
      // The reader captures paragraph style at its first text run.
      if (t.word in ["pard", "ql", "qc", "qr", "qj", "li", "ri", "sl"] and paragraph_content) {
        raise error("late RTF paragraph formatting requires source mode")
      }
      if (t.word in ["li", "ri", "sl"] and (t.parameter == null or t.parameter < 0)) {
        raise error("unsupported RTF paragraph measurement")
      }
      if (t.word == "par") { paragraph_content = false }
      else if (t.word in ["u", "line", "tab"] or rtf.punctuation[t.word] != null) { paragraph_content = true }
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
      else if (t.word in ["f", "fs"] and (depth != 1 or body)) {
        if (t.parameter == null or t.parameter < 0 or (t.word == "fs" and t.parameter == 0) or
            (t.word == "f" and not (contains(font_ids, t.parameter) ^ { false }))) {
          raise error("unsupported RTF font run")
        }
        body = true
      }
      else if (t.word in header_controls) {
        if (depth != 1 or body or t.parameter == null or t.parameter < 0 or (t.word == "fs" and t.parameter == 0) or
            (t.word == "f" and (len(font_ids) > 0)^ and not (contains(font_ids, t.parameter) ^ { false }))) {
          raise error("this RTF header control requires source mode")
        }
        // Retain document-wide fonts, language defaults and view preferences.
        retained_header = retained_header ++ "\\" ++ t.word ++ (string(t.parameter)^) ++ " "
      }
      else {
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
      if (depth > 0 and (replace(replace(t.value, "\r", ""), "\n", "") ^ { "" }) != "") { paragraph_content = true }
      cursor = t.end
    }
    else {
      body = true
      paragraph_content = true
      cursor = t.end
    }
    if (t.kind == "close") { cursor = t.end }
  }
  if (depth != 0 or not closed or not header or pending != null) raise error("incomplete RTF document") else retained_header
}

fn run_attrs(span, parsed) any^ {
  let style = span.style
  let font_changed = span.rtf_font != parsed.base_font
  let size_changed = span.rtf_size != parsed.base_size
  let font_css = rtf.font_run_style(parsed.fonts, span.rtf_font, span.rtf_size)^
  let declarations = [
    *if (inline_format.css_get(style, "color") != null) ["color: " ++ inline_format.css_get(style, "color")] else [],
    *if (font_changed and inline_format.css_get(font_css, "font-family") != null) ["font-family: " ++ inline_format.css_get(font_css, "font-family")] else [],
    *if (size_changed) ["font-size: " ++ inline_format.css_get(font_css, "font-size")] else []]
  if (font_changed and inline_format.css_get(font_css, "font-family") == null) { raise error("unknown RTF font") }
  else if (len(declarations) == 0) []
  else [mark('span', [
    {name: 'style', value: join(declarations, "; ")},
    *if (font_changed) [{name: 'rtf-font', value: span.rtf_font}] else [],
    *if (size_changed) [{name: 'rtf-size', value: span.rtf_size}] else []])]
}
fn span_marks(span, parsed) any^ => (
  let style = span.style,
  [
  *if (contains(style, "font-weight:700;") ^ { false }) [mark('strong', true)] else [],
  *if (contains(style, "font-style:italic;") ^ { false }) [mark('em', true)] else [],
  *if (contains(style, "underline") ^ { false }) [mark('u', true)] else [],
  *if (contains(style, "line-through") ^ { false }) [mark('del', true)] else [],
  *if (inline_format.css_get(style, "vertical-align") == "super") [mark('sup', true)] else [],
  *if (inline_format.css_get(style, "vertical-align") == "sub") [mark('sub', true)] else [],
  *run_attrs(span, parsed)^
])
fn paragraph_measure(style, property) any^ {
  let value = inline_format.css_get(style, property)
  if (value == null) null else int(round(float(slice(value, 0, len(value) - 2)^)^ * 15.0))
}
fn paragraph_attrs(style) any^ => [
  *if (inline_format.css_get(style, "text-align") in ["center", "right", "justify"])
    [{name:'align', value:inline_format.css_get(style, "text-align")}] else [],
  *[for (entry in [{name:'rtf-li', property:"margin-left"}, {name:'rtf-ri', property:"margin-right"},
                   {name:'rtf-sl', property:"line-height"}], let value = paragraph_measure(style, entry.property)^
    where value != null) {name:entry.name, value:value}]
]
fn to_model(parsed) map^ {
  let blocks = [for (block in content(parsed.document)^)
    node_attrs('p', paragraph_attrs(block.style)^,
      [for (span in content(block)^) text_marked(plain_text(span)^, span_marks(span, parsed)^)])];
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
  else if (mark_name == 'sup') "\\super"
  else if (mark_name == 'sub') "\\sub"
  else null
fn color_value(color) => if (not color.specified) null else
  "rgb(" ++ join([for (v in [color.red, color.green, color.blue]) string(v)], ",") ++ ")"
fn control_for_mark(m, envelope) any^ {
  if (m.name != 'span') mark_control(m.name)
  else {
    let css = inline_format.span_css([m])^
    let color = inline_format.css_get(css, "color")
    let matches = [for (i, entry in envelope.colors where ((color_value(entry) == color and color != null) ^ { false })) i]
    let font = if (m.value is array) attrs_get(m.value, 'rtf-font') else null
    let size = if (m.value is array) attrs_get(m.value, 'rtf-size') else null
    let expected = rtf.font_run_style(envelope.fonts, if (font == null) envelope.base_font else font,
                                      if (size == null) envelope.base_size else size)^
    let remaining = inline_format.css_set(inline_format.css_set(inline_format.css_set(css, "color", null), "font-family", null), "font-size", null)
    let invalid = not (m.value is array) or any([for (a in (if (m.value is array) m.value else []) where
      ((not (a.name in ['style', 'rtf-font', 'rtf-size'])) ^ { false })) true]) or remaining != "" or
      (m.value is array and len(m.value) != 1 + (if (font == null) 0 else 1) + (if (size == null) 0 else 1)) or
      (color != null and len(matches) == 0) or
      (font != null and (not (font is int) or inline_format.css_get(expected, "font-family") == null or
        inline_format.css_get(css, "font-family") != inline_format.css_get(expected, "font-family"))) or
      (size != null and (not (size is int) or size <= 0 or inline_format.css_get(css, "font-size") != inline_format.css_get(expected, "font-size"))) or
      (font == null and inline_format.css_get(css, "font-family") != null) or
      (size == null and inline_format.css_get(css, "font-size") != null) or
      (color == null and font == null and size == null)
    if (invalid) null else
      (if (font == null) "" else "\\f" ++ string(font)) ++
      (if (size == null) "" else "\\fs" ++ string(size)) ++
      (if (color == null) "" else "\\cf" ++ string(matches[0]))
  }
}
fn inline(item, envelope) any^ {
  if (is_text(item)) {
    let controls = [for (m in item.marks) control_for_mark(m, envelope)^]
    if (any([for (c in controls) c == null])) { raise error("RTF cannot represent a text style") }
    else if (len(controls) == 0) encoded(item.text)^
    else "{" ++ join(controls, "") ++ " " ++ encoded(item.text)^ ++ "}"
  }
  else if (is_node(item) and item.tag == 'br') "\\line "
  else if (is_node(item) and mark_control(item.tag) != null)
    "{" ++ mark_control(item.tag) ++ " " ++ join([for (child in item.content) inline(child, envelope)^], "") ++ "}"
  else raise error("RTF cannot represent this inline content")
}
fn paragraph_settings(block) => {
  align: attr_get(block, 'align') or "left", li: attr_get(block, 'rtf-li') or 0,
  ri: attr_get(block, 'rtf-ri') or 0, sl: attr_get(block, 'rtf-sl') or 0}
fn write_paragraph(block, previous, envelope) any^ {
  let settings = paragraph_settings(block)
  let invalid = any([for (a in block.attrs where ((not (a.name in ['align', 'rtf-li', 'rtf-ri', 'rtf-sl'])) ^ { false })) true]) or
    not (settings.align in ["left", "center", "right", "justify"]) or
    any([for (v in [settings.li, settings.ri, settings.sl]) not (v is int) or v < 0])
  if (invalid) raise error("RTF cannot represent this paragraph style")
  else (if (settings == previous) ""
    else "\\pard" ++ (if (settings.align == "center") "\\qc" else if (settings.align == "right") "\\qr"
      else if (settings.align == "justify") "\\qj" else "") ++
      (if (settings.li == 0) "" else "\\li" ++ string(settings.li)) ++
      (if (settings.ri == 0) "" else "\\ri" ++ string(settings.ri)) ++
      (if (settings.sl == 0) "" else "\\sl" ++ string(settings.sl)) ++ " ") ++
    join([for (child in block.content) inline(child, envelope)^], "")
}
fn write_document(doc, envelope) string^ {
  let bad = [for (block in doc.content where (not (is_node(block) ^ { false }) or block.tag != 'p')) block]
  if (len(bad) > 0) { raise error("RTF cannot represent this block") }
  else {
    let defaults = {align:"left", li:0, ri:0, sl:0}
    let paragraphs = [for (i, block in doc.content) write_paragraph(block,
      if (i == 0) defaults else paragraph_settings(doc.content[i - 1]), envelope)^]
    // An empty final block needs its own terminator; a final nonempty one does not.
    let terminal = if (len(doc.content) > 0 and all([for (child in doc.content[last].content)
      ((is_text(child) and child.text == "" and len(child.marks) == 0) ^ { false })])) "\\par " else ""
    "{\\rtf1\\ansi\\uc1 " ++ envelope.retained_header ++ join(paragraphs, "\\par ") ++ terminal ++ "}"
  }
}
pub fn import_text(source) map^ {
  let retained_header = validate(source)^
  // A final body font change must not become the baseline used by the writer.
  let base = rtf.parse_details("{\\rtf1\\ansi " ++ retained_header ++ "}", {preserve_empty: true, editor_runs: true})^
  let actual = rtf.parse_details(source, {preserve_empty: true, editor_runs: true})^
  let parsed = {*:actual, base_font: base.base_font, base_size: base.base_size}
  let known_font = len(base.fonts) == 0 or any([for (font in base.fonts) font.font_id == base.base_font]);
  if (not known_font) raise error("unknown RTF default font") else {
    let doc = to_model(parsed)^;
    {doc: doc, envelope: {source: source, original: doc, retained_header: retained_header, font_style: base.font_style,
                          colors: parsed.colors, fonts: parsed.fonts, base_font: base.base_font, base_size: base.base_size}}
  }
}
pub fn export_text(doc, envelope) any^ =>
  if (doc == envelope.original) envelope.source else (write_document(doc, envelope) ^ { null })
pub fn check_roundtrip(doc, envelope) any^ {
  let written = write_document(doc, envelope) ^ { null }
  if (written == null) "this RTF edit is not representable"
  else {
    let parsed = rtf.parse_details(written, {preserve_empty: true, editor_runs: true}) ^ { null }
    let back = if (parsed == null) null else (to_model({*:parsed, base_font:envelope.base_font, base_size:envelope.base_size}) ^ { null })
    if (back == null or html.normal_form(back) != html.normal_form(doc))
      "this RTF edit cannot round-trip" else null
  }
}
// RTF's editable blocks are paragraphs; apply one history transaction to the selection.
pub fn align_paragraphs(editor, align) any^ {
  let sel = editor.selection
  if (sel == null or not (sel.kind in ['all', 'node', 'text']) or
      not (align in ["left", "center", "right", "justify"])) { null }
  else {
    let lo = (if (sel.kind == 'all') 0 else if (sel.kind == 'node') sel.path[0] else pos_min(sel.anchor, sel.head).path[0])^
    let hi = (if (sel.kind == 'all') len(editor.doc.content) - 1 else if (sel.kind == 'node') sel.path[0]
      else if (len(sel.anchor.path) == 2 and len(sel.head.path) == 2) commands.selected_top_block_end(sel)^
      else pos_max(sel.anchor, sel.head).path[0])^
    let value = if (align == "left") null else align
    let steps = [for (i in lo to hi, let p = editor.doc.content[i] where
      ((p != null and is_node(p) and p.tag == 'p' and attr_get(p, 'align') != value) ^ { false }))
      step_set_attr([i], 'align', value)]
    if (len(steps) == 0) null else commands.tx_apply_steps(tx_begin(editor.doc, sel), steps, 0, len(steps))^
  }
}
pub let descriptor = {id: 'rtf', name: "RTF", suffixes: [".rtf"], surface: 'rich_text', fallback_source: true,
  schema: html.schema, schema_preset: 'html5_subset', underline: true, view_only_tags: [],
  unsupported_input_types: ["formatBlock", "insertUnorderedList", "insertOrderedList", "formatIndent", "formatOutdent",
    "formatBlockquote", "formatLiftBlockquote", "insertLink", "insertImage", "insertCodeBlock", "insertHorizontalRule",
    "insertTable", "insertTableRow", "deleteTableRow", "insertTableColumn", "deleteTableColumn", "modelToggleMark"],
  allowed_model_marks: ['del'],
  surface_style: (envelope) => envelope.font_style,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip, align_paragraphs: align_paragraphs}
