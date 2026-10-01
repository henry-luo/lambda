// doc_editor.ls — Phase 1 document-editor prototype
//
// A read-only project browser built with Lambda's reactive UI.  The left
// panel is rooted at the current project directory; directories are loaded
// only when opened so the prototype remains useful for large worktrees.
//
// Run:
//   ./lambda.exe view test/ui/doc_editor.html
// Restrained splash variant:
//   ./lambda.exe view test/ui/doc_editor_calm.html
// Direct view without the startup splash:
//   ./lambda.exe view test/ui/doc_editor.ls
// Headless smoke:
//   ./lambda.exe view test/ui/doc_editor.ls --headless --no-log

import pdf: lambda.pdf.pdf
import dom
import pdf_html: lambda.pdf.html
import latex: lambda.latex.latex
import latex_css: lambda.latex.css
import math_renderer: lambda.doc.math.math
import math_css: lambda.doc.math.css
import graph_doc: lambda.graph.document
import tikz: lambda.doc.tikz.tikz

let PROJECT_ROOT = "."

// --------------------------------------------------------------------------
// Filesystem and selection helpers
// --------------------------------------------------------------------------

fn directory_entries(path) {
  let entries = input(path, 'dir') ^ { [] };
  // The directory reader sorts names; keep that order within each group.
  [for (entry in entries where entry.is_dir) entry] ++
    [for (entry in entries where not entry.is_dir) entry]
}
fn child_path(parent_path, child_name) => join([parent_path, child_name], "/")
fn absolute_file_path(path) => sys.proc.self.cwd# ++ "/" ++ path

// Keep the root list identity stable so document selection does not recreate
// the mounted directory-row components.
let PROJECT_ENTRIES = directory_entries(PROJECT_ROOT)

fn path_is_open(open_paths, path) => contains(open_paths, path) or false
fn tree_hit_class(path) => "tree-hit-" ++ replace(replace(path, "/", "_"), ".", "_")
// An empty-space hit targets the row; absent event classes discharge to false (S7.9.2-3).
fn event_hits_row(evt, hit_class) =>
  (contains(evt["target_class"], hit_class ++ " ") or
   contains(evt["target_parent_class"], hit_class ++ " ")) or false

// A tree-wide redraw needs the expansion currently owned by individual rows.
pn visible_open_paths(target) {
  let rows = dom.query_selector_all(dom.root_node(target), ".tree-entry");
  [for (row in rows where dom.get_attribute(row, "data-tree-open") == "true")
    dom.get_attribute(row, "data-tree-path")]
}

pn reset_document_scroll(target) {
  let root = dom.root_node(target)
  let preview = dom.query_selector(root, ".rendered-preview")
  let source = dom.query_selector(root, ".source-tab-panel")
  // The preview element is reused by the reactive render, including its scroll state.
  if (preview != null) { dom.set_scroll_state(preview, 0.0, 0.0) }
  if (source != null) { dom.set_scroll_state(source, 0.0, 0.0) }
}

fn pgf_text_width_px(target) {
  let panel = dom.query_selector(dom.root_node(target), ".document-panel")
  let box = if (panel == null) null else dom.bounding_box(panel)
  let width = if (box == null) null else box.width
  // PGFPlots dimensions such as 0.8\textwidth need the current document pane.
  if ((type(width) == int or type(width) == float) and width > 0) width else null
}

fn bounded_text_offset(value, offset) {
  if (offset < 0) { 0 }
  else if (offset > len(value)) { len(value) }
  else { offset }
}

fn text_before_input(value, evt) {
  let selection_start = evt["selection_start"]
  let caret_pos = evt["caret_pos"]
  // Optional event offsets can be undefined; only integers are valid slice indices.
  if (type(selection_start) == int) { bounded_text_offset(value, selection_start) }
  else if (type(caret_pos) == int) { bounded_text_offset(value, caret_pos) }
  else { len(value) }
}

fn text_after_input(value, evt) {
  let selection_end = evt["selection_end"]
  if (type(selection_end) == int) { bounded_text_offset(value, selection_end) }
  else { text_before_input(value, evt) }
}

fn erase_backwards(value, evt) {
  let start = text_before_input(value, evt)
  let end = text_after_input(value, evt)
  if (start != end) { slice(value, 0, start) ++ slice(value, end, len(value)) }
  else if (start > 0) { slice(value, 0, start - 1) ++ slice(value, start, len(value)) }
  else { value }
}

fn erase_forwards(value, evt) {
  let start = text_before_input(value, evt)
  let end = text_after_input(value, evt)
  if (start != end) { slice(value, 0, start) ++ slice(value, end, len(value)) }
  else if (start < len(value)) { slice(value, 0, start) ++ slice(value, start + 1, len(value)) }
  else { value }
}

fn entry_is_visible(entry) {
  let entry_name = lower(entry["name"])
  // Hide implementation artefacts without preventing normal source browsing:
  // dot entries, build/release output directories, and *.exe binaries.
  not starts_with(entry_name, ".") and
    (not entry["is_dir"] or
      (not starts_with(entry_name, "build") and not starts_with(entry_name, "release"))) and
    (entry["is_dir"] or not ends_with(entry_name, ".exe"))
}

fn entry_matches_filter(entry, filter_text) {
  // Directories remain visible so a search result can be reached by expanding
  // its ancestry; the filter itself applies to file names.
  entry_is_visible(entry) and
    (entry["is_dir"] or filter_text == "" or contains(lower(entry["name"]), lower(filter_text)))
}

fn document_format(extension) {
  let ext = lower(extension)
  if (contains(["md", "markdown", "mdown", "mkdn"], ext)) { "markdown" }
  else if (contains(["wiki", "mediawiki"], ext)) { "wiki" }
  else if (contains(["rst", "rest"], ext)) { "rst" }
  else if (ext == "org") { "org" }
  else if (contains(["adoc", "asciidoc", "asc"], ext)) { "asciidoc" }
  else if (ext == "man" or contains(["1", "2", "3", "4", "5", "6", "7", "8", "9", "1m", "3p"], ext)) { "man" }
  else if (ext == "textile") { "textile" }
  else if (ext == "rtf") { "rtf" }
  else if (contains(["htm", "html"], ext)) { "html" }
  else { null }
}

fn is_latex_document(extension) {
  let ext = lower(extension)
  ext == "tex" or ext == "latex"
}

fn is_pdf_document(extension) => lower(extension) == "pdf"
fn is_eml_document(extension) => lower(extension) == "eml"
fn is_pgf_document(extension) => lower(extension) == "pgf"
fn is_image_document(extension) => contains(["png", "jpg", "jpeg", "gif", "svg"], lower(extension)) or false
fn is_raster_document(extension) => is_image_document(extension) and lower(extension) != "svg"
fn is_table_document(extension) => contains(["csv", "tsv"], lower(extension)) or false
fn graph_flavor(extension) {
  let ext = lower(extension)
  if (ext == "mmd") { "mermaid" }
  else if (ext == "dot") { "dot" }
  else if (ext == "d2") { "d2" }
  else { null }
}

fn property_format(extension) {
  let ext = lower(extension)
  if (ext == "json") { "json" }
  else if (ext == "xml") { "xml" }
  else if (contains(["yaml", "yml"], ext)) { "yaml" }
  else if (ext == "toml") { "toml" }
  else if (ext == "ini") { "ini" }
  else if (contains(["properties", "props"], ext)) { "properties" }
  else if (contains(["ics", "ical"], ext)) { "ics" }
  else if (ext == "vcf") { "vcf" }
  else { null }
}

// Java properties are flat; the inspector nests dotted keys into groups. A group
// is {props, has_own, own}: `own` keeps the value of a key that is also a prefix
// (log4j `a.b=x` beside `a.b.c=y`). Parsed values are scalars, so in this format
// every map is a group.
fn properties_group(children, own) =>
  {props: children, has_own: len(own) > 0, own: (if (len(own) > 0) own[0].value else null)}
fn properties_node(name, members, depth) {
  let own = [for (entry in members where len(entry.path) == depth + 1) entry]
  let deeper = [for (entry in members where len(entry.path) > depth + 1) entry]
  if (len(deeper) == 0) { {name:name, value:own[0].value} }
  else { {name:name, value:properties_group(properties_nodes(deeper, depth + 1), own)} }
}
fn properties_nodes(entries, depth) {
  let names = unique([for (entry in entries) entry.path[depth]]);
  [for (name in names)
    properties_node(name, [for (entry in entries where entry.path[depth] == name) entry], depth)]
}
fn properties_tree(flat) {
  if (type(flat) != map) { flat }
  else {
    let entries = [for (key, value in flat) {path:split(string(key), "."), value:value}]
    properties_group(properties_nodes(entries, 0), [])
  }
}

fn is_renderable_document(extension) =>
  document_format(extension) != null or is_latex_document(extension) or
    is_pdf_document(extension) or is_eml_document(extension) or
    is_pgf_document(extension) or is_image_document(extension) or
    graph_flavor(extension) != null or property_format(extension) != null or is_table_document(extension)

// seti private-use glyphs; keep codepoints readable alongside the bundled font.
let SETI_CLOCK = chr(0xE012)
let SETI_CONFIG = chr(0xE019)
let SETI_CSS = chr(0xE01D)
let SETI_CSV = chr(0xE01E)
let SETI_DB = chr(0xE022)
let SETI_HTML = chr(0xE048)
let SETI_IMAGE = chr(0xE04C)
let SETI_INFO = chr(0xE04D)
let SETI_JSON = chr(0xE055)
let SETI_MARKDOWN = chr(0xE060)
let SETI_PDF = chr(0xE06D)
let SETI_PIPELINE = chr(0xE071)
let SETI_REACT = chr(0xE07D)
let SETI_SVG = chr(0xE091)
let SETI_TEX = chr(0xE094)
let SETI_WORD = chr(0xE0A3)
let SETI_XML = chr(0xE0A5)
let SETI_YAML = chr(0xE0A7)

fn file_icon(extension) {
  let ext = if (extension == null) "" else lower(extension)
  // seti glyphs identify parsed formats; source-only files retain the plain-text icon.
  if (ext == "ls") { "λ" }
  else if (ext == "json") { SETI_JSON }
  else if (contains(["yaml", "yml"], ext)) { SETI_YAML }
  else if (contains(["toml", "ini", "properties", "props"], ext)) { SETI_CONFIG }
  else if (is_table_document(ext)) { SETI_CSV }
  else if (ext == "xml") { SETI_XML }
  else if (contains(["db", "sqlite", "sqlite3"], ext)) { SETI_DB }
  else if (contains(["md", "markdown", "mdown", "mkdn", "mdx", "wiki", "mediawiki",
                     "rst", "rest", "org", "adoc", "asciidoc", "asc", "textile", "txtl",
                     "m", "mk", "mark", "typ", "typst"], ext) or
           ext == "man" or contains(["1", "2", "3", "4", "5", "6", "7", "8", "9", "1m", "3p"], ext)) { SETI_MARKDOWN }
  else if (contains(["htm", "html"], ext)) { SETI_HTML }
  else if (ext == "rtf") { SETI_WORD }
  else if (is_latex_document(ext)) { SETI_TEX }
  else if (is_pdf_document(ext)) { SETI_PDF }
  else if (is_pgf_document(ext)) { SETI_SVG }
  else if (contains(["mmd", "dot", "gv", "d2", "dsl", "structurizr"], ext)) { SETI_PIPELINE }
  else if (ext == "svg") { SETI_SVG }
  else if (contains(["png", "jpg", "jpeg", "gif", "bmp", "tif", "tiff", "webp", "ico"], ext)) { SETI_IMAGE }
  else if (ext == "css") { SETI_CSS }
  else if (ext == "jsx") { SETI_REACT }
  else if (contains(["vcf", "vcard", "eml"], ext)) { SETI_INFO }
  else if (contains(["ics", "ical"], ext)) { SETI_CLOCK }
  else { "▤" }
}

fn file_icon_color(icon) {
  if (icon == SETI_JSON) "#cbcb41"
  else if (icon == SETI_YAML or icon == SETI_IMAGE or icon == SETI_SVG) "#a074c4"
  else if (icon == SETI_CSV) "#8dc149"
  else if (icon == SETI_PDF) "#cc3e44"
  else if (icon == SETI_XML or icon == SETI_PIPELINE) "#e37933"
  else if (icon == SETI_DB) "#f55385"
  else if (icon == SETI_CONFIG) "#6d8086"
  else if (icon == "λ") "#b7a0ff"
  else "#519aba"
}

fn selected_source(file) {
  if (file == null) { "" }
  else {
    let selected_path = file["file_path"];
    input(selected_path, 'text') ^ { "Unable to read selected file" }
  }
}

fn xml_has_stylesheet(parsed) {
  if (parsed == null) { false }
  else {
    // The parser preserves the stylesheet processing instruction as a document child.
    any([for (child in content(parsed) where type(child) == element and
      string(name(child)) == "?xml-stylesheet" and len(content(child)) > 0)
      contains(content(child)[0], "href=")])
  }
}

fn selected_preview(file) {
  if (file == null) { null }
  else {
    let selected_path = file["file_path"]
    let format = document_format(file["extension"])
    let flavor = graph_flavor(file["extension"])
    if (is_image_document(file["extension"])) {
      <img src:absolute_file_path(selected_path), alt:file["name"]>
    }
    else if (is_pdf_document(file["extension"])) {
      // Render in this document's runtime; a nested PDF iframe cannot start another runtime.
      let parsed = input(selected_path, 'pdf') ^ { null }
      if (parsed == null) { <p class:"preview-error", "Unable to read selected PDF"> }
      else { pdf.pdf_to_html(parsed, null) ^ { <p class:"preview-error", "Unable to render selected PDF"> } }
    }
    else if (is_latex_document(file["extension"])) {
      // The editor already owns the runtime, so render LaTeX inline instead of loading an iframe.
      let parsed = input(selected_path, 'latex') ^ { null }
      if (parsed == null) { <p class:"preview-error", "Unable to read selected LaTeX"> }
      else { latex.render(parsed, null) ^ { <p class:"preview-error", "Unable to render selected LaTeX"> } }
    }
    else if (is_pgf_document(file["extension"])) {
      // Reuse the parsed TikZ document so PGF fragments render inside the editor runtime.
      let parsed = input(selected_path, {type:"tikz"}) ^ { null }
      if (parsed == null) { <p class:"preview-error", "Unable to read selected PGF"> }
      else { tikz.render_document(parsed, {text_width_px:file["text_width_px"]}) ^ {
        <p class:"preview-error", "Unable to render selected PGF: " ++ ^.message>
      } }
    }
    else if (flavor != null) {
      // The graph document adapter installs Radiant's layout for this inline preview.
      let parsed = input(selected_path, {type:"graph", flavor:flavor}) ^ { null }
      if (parsed == null) { <p class:"preview-error", "Unable to read selected graph"> }
      else { graph_doc.to_html(parsed, null) ^ { <p class:"preview-error", "Unable to render selected graph"> } }
    }
    else if (format == null) { null }
    else { input(selected_path, format) ^ { <p class:"preview-error", "Unable to render selected file"> } }
  }
}

fn property_children(value, format) {
  if (format == "xml" and type(value) == element) {
    // XML attributes precede content; numbered segments distinguish repeated sibling tags.
    let attrs = [for (key, child at map(value)) {name:"@" ++ string(key), value:child}]
    let attr_rows = [for (index, attr in attrs)
      {name:attr.name, value:attr.value, segment:"a" ++ string(index)}]
    let child_rows = [for (index, child in content(value))
      {name:(if (type(child) == element) string(name(child)) else "#text"),
       value:child, segment:"c" ++ string(index)}]
    attr_rows ++ child_rows
  } else if (format == "properties" and type(value) == map) {
    [for (index, child in value.props) {name:child.name, value:child.value, segment:string(index)}]
  } else if (format == "vcf" and type(value) == map and type(value["contacts"]) == array) {
    [for (index, contact in value["contacts"])
      {name:"VCARD", value:contact, segment:string(index)}]
  } else if (contains(["ics", "vcf"], format) and type(value) == map and
             type(value["entries"]) == array) {
    // RFC content lines retain order, repeated names, and their parameters.
    [for (index, entry in value["entries"])
      {name:(if (format == "vcf") upper(entry["name"]) else entry["name"]),
       value:(if (entry["parameters"] != null)
         {value:entry["value"], parameters:entry["parameters"]} else entry["value"]),
       segment:string(index)}]
  } else if (type(value) == map) {
    // Numeric segments keep expand state distinct for keys containing punctuation.
    let fields = [for (key, child in value) {name:string(key), value:child}];
    [for (index, field in fields)
      {name:field.name, value:field.value, segment:string(index)}]
  } else if (type(value) == array) {
    [for (index, child in value) {name:string(index), value:child, segment:string(index)}]
  } else { [] }
}

fn property_is_container(value) =>
  type(value) == map or type(value) == array or
    (type(value) == element and
      (len(map(value)) > 0 or any([for (child in content(value)) type(child) == element])))
fn property_roots(parsed, format) {
  if (format == "xml") {
    // The XML reader wraps the document and its processing instructions in <document>.
    [for (index, child in content(parsed)
      where type(child) == element and
        not starts_with(string(name(child)), "?") and
        not starts_with(string(name(child)), "!"))
      {name:string(name(child)), value:child, segment:string(index)}]
  }
  else if (format == "ics") { [{name:"VCALENDAR", value:parsed, segment:"calendar"}] }
  else if (format == "vcf" and type(parsed["contacts"]) != array) {
    [{name:"VCARD", value:parsed, segment:"card"}]
  } else { property_children(parsed, format) }
}
fn property_initial_open_paths(parsed, format) {
  if (not property_is_container(parsed)) { [] }
  else {
    let roots = property_roots(parsed, format)
    // Expand a lone top-level group once, while leaving later toggles in user control.
    if (len(roots) == 1 and property_is_container(roots[0].value)) [roots[0].segment] else []
  }
}
let PROPERTY_PAGE_SIZE = 60
fn property_visible_count(path, more_paths) =>
  PROPERTY_PAGE_SIZE * (1 + len([for (more_path in more_paths where more_path == path) more_path]))
fn property_hit_class(path) =>
  "property-hit-" ++ replace(replace(replace(path, "/", "_"), ".", "_"), " ", "_")

fn property_matches(value, name, filter_text, format) {
  if (filter_text == "" or contains(lower(name), filter_text)) { true }
  else if (property_is_container(value)) {
    any([for (child in property_children(value, format))
      property_matches(child.value, child.name, filter_text, format)])
  } else { false }
}

fn property_value_text(value) {
  if (type(value) == string) { value }
  else if (value == null) { "null" }
  else { string(value) }
}

fn property_summary(value, format) {
  if (format == "xml" and type(value) == element) {
    let children = content(value)
    let attrs = len(map(value))
    let text = if (len(children) == 1 and type(children[0]) == string) children[0] else null
    let detail = if (text != null) text
      else if (len(children) > 0) string(len(children)) ++
        (if (len(children) == 1) " node" else " nodes")
      else ""
    let attr_detail = if (attrs == 0) "" else string(attrs) ++
      (if (attrs == 1) " attribute" else " attributes")
    if (detail == "") { if (attr_detail == "") "Empty element" else attr_detail }
    else if (attr_detail == "") { detail }
    else { detail ++ " · " ++ attr_detail }
  }
  else if (format == "properties" and type(value) == map) {
    let count = string(len(value.props)) ++ " properties"
    if (value.has_own) { property_value_text(value.own) ++ " · " ++ count } else { count }
  }
  else if (contains(["ics", "vcf"], format) and type(value) == map and
      value["parameters"] != null and value["value"] != null) {
    property_value_text(value["value"])
  }
  else if (format == "vcf" and type(value) == map and type(value["entries"]) == array) {
    if (value["full_name"] != null) { value["full_name"] }
    else { string(len(value["entries"])) ++ " fields" }
  }
  else if (format == "vcf" and type(value) == map and type(value["contacts"]) == array) {
    string(len(value["contacts"])) ++ " contacts"
  }
  else if (format == "ics" and type(value) == map and type(value["entries"]) == array) {
    let label = value["summary"]
    if (label != null) { label }
    else { string(len(value["entries"])) ++ " entries" }
  }
  else if (type(value) == map) { "Object · " ++ string(len(value)) ++ " properties" }
  else if (type(value) == array) { "Array · " ++ string(len(value)) ++ " items" }
  else { property_value_text(value) }
}

// CSV input returns named maps for header rows and positional arrays otherwise.
fn csv_columns(rows) {
  if (type(rows) != array or len(rows) == 0) { [] }
  else if (type(rows[0]) == map) {
    [for (key, value in rows[0]) {key:string(key), label:string(key)}]
  } else if (type(rows[0]) == array) {
    [for (index, value in rows[0]) {key:index, label:"Column " ++ string(index + 1)}]
  } else { [] }
}
fn csv_cell(row, column) {
  if (type(row) == map or type(row) == array) { row[column.key] }
  else { null }
}
let CSV_DEFAULT_WIDTH = 150
let CSV_MIN_WIDTH = 72
let CSV_PAGE_SIZE = 200
fn csv_width(widths, index) =>
  if (index < len(widths)) widths[index] else CSV_DEFAULT_WIDTH
fn csv_resized_widths(widths, gesture, x) {
  let width = max(CSV_MIN_WIDTH, gesture.width + x - gesture.x);
  [for (index, previous in widths) if (index == gesture.index) width else previous]
}

// --------------------------------------------------------------------------
// Mark document preview templates
//
// Markup readers produce the same HTML-shaped Mark
// vocabulary. LaTeX returns its normal HTML elements after rendering.
// Applying it deliberately keeps parser output separate from this prototype's
// UI chrome.
// --------------------------------------------------------------------------

view any { ~ }

fn rendered_children(item) => [for (child in content(item)) apply(child)]

view <doc> { <div class:"document-body", *[rendered_children(~)]> }
// HTML previews expose document content while keeping imported page chrome and
// executable/style nodes from altering the editor itself.
view <html> { <div class:"document-body", *[rendered_children(~)]> }
view <head> { "" }
view <title> { "" }
view <script> { "" }
view <style> { "" }
view <body> { <div class:"document-body", *[rendered_children(~)]> }
view <h1> { <h1 id:~.id, *[rendered_children(~)]> }
view <h2> { <h2 id:~.id, *[rendered_children(~)]> }
view <h3> { <h3 id:~.id, *[rendered_children(~)]> }
view <h4> { <h4 id:~.id, *[rendered_children(~)]> }
view <h5> { <h5 id:~.id, *[rendered_children(~)]> }
view <h6> { <h6 id:~.id, *[rendered_children(~)]> }
view <p> { <p style:~.style, *[rendered_children(~)]> }
view <div> { <div class:~.class, style:~.style, *[rendered_children(~)]> }
view <nav> { <nav class:~.class, *[rendered_children(~)]> }
view <dl> { <dl *[rendered_children(~)]> }
view <dt> { <dt *[rendered_children(~)]> }
view <dd> { <dd *[rendered_children(~)]> }
// TikZ uses positioned spans for the drawing frame and math labels.
view <span> { <span class:~.class, style:~.style, *[rendered_children(~)]> }
view <strong> { <strong *[rendered_children(~)]> }
view <em> { <em *[rendered_children(~)]> }
view <del> { <del *[rendered_children(~)]> }
view <u> { <u *[rendered_children(~)]> }
view <code> {
  if (~.type == "block") { <pre <code *[rendered_children(~)]>> }
  else { <code *[rendered_children(~)]> }
}
view <math> {
  // Markup readers keep TeX source in math nodes; parse it for the shared typesetter.
  let source = if (len(content(~)) > 0) content(~)[0] else ""
  let parsed = if (~.flavor == "ascii") { null }
    else { parse(source, {type: "math", flavor: "latex"}) ^ { null } }
  let display = ~.type == "block"
  let rendered = if (parsed == null) { null }
    else { math_renderer.render_math(parsed, {display: display}) ^ { null } }
  let body = if (rendered == null) { <code class:"math-error", source> } else { rendered }
  if (display) { <div class:"math-display-container", body> }
  else { body }
}
view <pre> { <pre *[rendered_children(~)]> }
view <ul> { <ul *[rendered_children(~)]> }
view <ol> { <ol *[rendered_children(~)]> }
view <li> { <li *[rendered_children(~)]> }
view <blockquote> { <blockquote *[rendered_children(~)]> }
view <a> { <a href:~.href, *[rendered_children(~)]> }
view <img> { <img src:~.src, alt:~.alt> }
view <br> { <br> }
view <hr> { <hr> }
view <table> { <table *[rendered_children(~)]> }
view <caption> { <caption *[rendered_children(~)]> }
view <thead> { <thead *[rendered_children(~)]> }
view <tbody> { <tbody *[rendered_children(~)]> }
view <tfoot> { <tfoot *[rendered_children(~)]> }
view <tr> { <tr *[rendered_children(~)]> }
view <th> { <th *[rendered_children(~)]> }
view <td> { <td *[rendered_children(~)]> }

// The CSV reader supplies decoded cells, so quoting and tab separators share one view.
view <csv_preview> {
  let columns = csv_columns(~.rows)
  // Mount a bounded number of rows so large timing tables remain interactive.
  let row_count = if (type(~.rows) == array) len(~.rows) else 0
  let visible_rows = if (row_count == 0) [] else take(~.rows, ~.visible_rows)
  let table_width = sum([for (index, column in columns) csv_width(~.widths, index)]);
  <section id:"csv-preview", class:"rendered-preview csv-preview"
  , if (len(columns) == 0) {
      <p class:"csv-empty", "No rows to display">
    } else {
      <table class:"csv-table", style:("width:" ++ string(table_width) ++ "px")
      , <thead <tr
          for (index, column in columns) {
              let width = csv_width(~.widths, index);
              <th class:"csv-header", style:("width:" ++ string(width) ++ "px")
              , <span class:"csv-header-label", column.label>
                <span class:"csv-resize", 'data-csv-column':string(index),
                  title:("Resize " ++ column.label), "">
              >
            }
          >
        >
        <tbody
        for (row in visible_rows) {
            <tr
            for (index, column in columns) {
                let cell = csv_cell(row, column);
                <td style:("width:" ++ string(csv_width(~.widths, index)) ++ "px"),
                  if (cell == null) "" else property_value_text(cell)>
              }
            >
          }
        >
      >
      if (row_count > len(visible_rows)) {
        <button class:"csv-more", "Show more rows (" ++
          string(row_count - len(visible_rows)) ++ " remaining)">
      }
    }
  >
}
on click(evt) {
  if (contains(evt.target_class, "csv-more")) { emit("csv_more_rows", null) }
}
on mousedown(evt) {
  if (evt.button != 0 or not contains(evt.target_class, "csv-resize")) { return }
  let column_id = dom.get_attribute(evt.target, "data-csv-column")
  let indices = [for (index, column in csv_columns(~.rows) where string(index) == column_id) index]
  if (len(indices) == 0) { return }
  let index = indices[0]
  emit("csv_resize_start", {index:index, x:evt.x, width:csv_width(~.widths, index)})
  'prevent-default'
}
on mousemove(evt) { emit("csv_resize_move", evt.x) }

// --------------------------------------------------------------------------
// Structured property inspector
// --------------------------------------------------------------------------

view <property_node> {
  let expandable = property_is_container(~.value)
  let hit_class = property_hit_class(~.path)
  let row_class = if (expandable) hit_class ++ " property-row property-expandable"
    else "property-row"
  let is_open = expandable and
    (if (~.filter_text == "") { path_is_open(~.open_paths, ~.path) }
     else { not path_is_open(~.closed_paths, ~.path) })
  let matching_children = if (is_open) {
    [for (child in property_children(~.value, ~.format)
      where property_matches(child.value, child.name, ~.filter_text, ~.format)) child]
  } else { [] }
  // Bound mounted rows; broad filters can match thousands of keys at once.
  let visible_children = take(matching_children, property_visible_count(~.path, ~.more_paths))
  let row_style = "padding-left:" ++ string(~.depth * 18 + 16) ++ "px";
  let summary = property_summary(~.value, ~.format);

  <div class:"property-entry", 'data-property-name':~.name
  , <div class:row_class, style:row_style
    , if (expandable) {
        <button class:("property-toggle " ++ hit_class ++ " property-control"),
          'aria-label':((if (is_open) "Collapse " else "Expand ") ++ ~.name),
          'aria-expanded':(if (is_open) "true" else "false"),
          if (is_open) "▾" else "▸">
      } else { <span class:"property-spacer", ""> }
      <span class:"property-name", ~.name>
      <span class:(if (expandable) "property-value property-summary" else "property-value"),
        title:summary, summary>
    >
    if (is_open and len(matching_children) > 0) {
      <div class:"property-children"
      , for (child in visible_children)
          apply(<property_node name:child.name, value:child.value,
            path:(~.path ++ "/" ++ child.segment), depth:(~.depth + 1),
            filter_text:~.filter_text, open_paths:~.open_paths,
            closed_paths:~.closed_paths, more_paths:~.more_paths, format:~.format>)
        if (len(matching_children) > len(visible_children)) {
          <button class:("property-more " ++ hit_class ++ " property-control"),
            (if (~.filter_text == "") "Show more properties (" else "Show more matching properties (") ++
            string(len(matching_children) - len(visible_children)) ++ " remaining)">
        }
      >
    }
  >
}
on click(evt) {
  if (contains(evt.target_class, "property-more ") and
      contains(evt.target_class, property_hit_class(~.path) ++ " ")) {
    emit("property_more", ~.path)
  } else if (expandable and event_hits_row(evt, hit_class)) {
    emit("property_toggle", {path:~.path, is_open: is_open})
  }
}

view <property_inspector> {
  let format = property_format(~.file["extension"])
  let parsed = ~.parsed
  let query = lower(~.filter_text);
  <section id:"property-preview", class:"rendered-preview property-preview"
  , <div class:"property-filter-bar"
    , <span class:"property-filter-icon", 'aria-hidden':"true", "⌕">
      <input id:"property-filter", type:"search", class:"property-filter",
        value:~.filter_text, placeholder:"Filter property names",
        'aria-label':"Filter property names">
      if (~.filter_text != "") {
        <button class:"property-filter-clear", title:"Clear property filter", "×">
      }
    >
    if (parsed == null) {
      <p class:"property-message preview-error", "Unable to parse selected file">
    } else if (not property_is_container(parsed)) {
      if (~.filter_text == "" or contains("value", lower(~.filter_text))) {
        <div class:"property-row property-root-value"
        , <span class:"property-name", "value">
          <span class:"property-value", property_value_text(parsed)>
        >
      } else { <p class:"property-message", "No matching properties"> }
    } else {
      let roots = property_roots(parsed, format)
      let matches = [for (child in roots
        where property_matches(child.value, child.name, query, format)) child]
      let visible_roots = take(matches, property_visible_count("", ~.more_paths))
      if (len(matches) == 0) {
        <p class:"property-message", "No matching properties">
      } else {
        <div class:"property-list"
        , for (child in visible_roots)
            apply(<property_node name:child.name, value:child.value,
              path:child.segment, depth:0, filter_text:query,
              open_paths:~.open_paths, closed_paths:~.closed_paths,
              more_paths:~.more_paths, format:format>)
          if (len(matches) > len(visible_roots)) {
            <button class:"property-more property-root-more",
              (if (query == "") "Show more properties (" else "Show more matching properties (") ++
              string(len(matches) - len(visible_roots)) ++ " remaining)">
          }
        >
      }
    }
  >
}
on click(evt) {
  if (contains(evt.target_class, "property-root-more")) { emit("property_more", "") }
}

fn eml_html_root(body) {
  // HTML5 returns #document; enter at html so the editor's element views run.
  let roots = [for (child in content(body) where type(child) == element and name(child) == 'html') child]
  if (len(roots) > 0) roots[0] else body
}

view <eml_preview> {
  let message = ~.message
  let headers = if (message != null and type(message["headers"]) == map) message["headers"] else null;
  let body = if (message == null) null else message["body"];
  <section id:"eml-preview", class:"rendered-preview eml-preview"
  , if (message == null) {
      <p class:"preview-error", "Unable to read selected email">
    } else {
      <div class:"eml-message"
      , if (headers != null) {
          <table class:"eml-header-table"
          , <tbody
              for (header, value in headers) {
                <tr 'data-header-name':string(header)
                , <th scope:"row", string(header)>
                  <td property_value_text(value)>
                >
              }
            >
          >
        }
        // HTML MIME bodies are parsed Mark elements; strings retain plain-text wrapping.
        if (type(body) == element) {
          <div id:"eml-body", class:"eml-html-body", apply(eml_html_root(body))>
        } else {
          <pre id:"eml-body", class:"eml-body",
            if (body != null) body else "">
        }
      >
    }
  >
}

// --------------------------------------------------------------------------
// Lazy file-tree rows
// --------------------------------------------------------------------------

view <tree_entry> state children: null, is_open: ~.initial_open {
  let icon_text = if (~.is_dir) "" else file_icon(~.extension)
  let icon_class = if (~.is_dir) "tree-icon folder-icon"
    else if (icon_text == "▤") "tree-icon file-icon default-icon"
    else if (icon_text == "λ") "tree-icon file-icon lambda-icon"
    else "tree-icon file-icon seti-icon"
  let icon_style = if (~.is_dir or icon_text == "▤") "" else "color:" ++ file_icon_color(icon_text)
  let entry_path = child_path(~.parent_path, ~.name)
  let indent = (~.depth * 16) ++ "px"
  let hit_class = tree_hit_class(entry_path)
  let row_class = if (~.selected_path == entry_path) {
    hit_class ++ " tree-row selected"
  } else {
    hit_class ++ " tree-row"
  }
  let matching_children = if (~.is_dir and is_open) {
    let current_children = if (children == null) { directory_entries(entry_path) } else { children };
    [for (child in current_children where entry_matches_filter(child, ~.filter_text)) child]
  } else { [] };

  <div class:"tree-entry", 'data-tree-path':entry_path,
    'data-tree-open':(if (~.is_dir and is_open) "true" else "false")
  , <div class:row_class, style:("padding-left:" ++ indent)
    , if (~.is_dir) {
        <button class:"tree-toggle",
          if (is_open) "▾" else "▸">
      } else {
        <span class:"tree-spacer", "">
      }
      <span class:icon_class, style:icon_style, 'aria-hidden':"true",
        if (~.is_dir) "▣" else icon_text>
      <span class:"tree-label", ~.name>
    >
    if (~.is_dir and is_open) {
      <div class:"tree-children"
      , for (child in matching_children)
          apply(<tree_entry
            // Recompute per child: retaining entry_path here appends a prior
            // sibling during reactive list reconciliation.
            parent_path:child_path(~.parent_path, ~.name),
            name:child.name,
            extension:child.extension,
            is_dir:child.is_dir,
            depth:(~.depth + 1),
            initial_open:path_is_open(~.open_paths,
                                      child_path(entry_path, child.name)),
            open_paths:~.open_paths,
            selected_path:~.selected_path,
            filter_text:~.filter_text
          >)
      >
    }
  >
}
on click(evt) {
  let entry_path = child_path(~.parent_path, ~.name)
  let hit_class = tree_hit_class(entry_path)
  let hits_this_row = event_hits_row(evt, hit_class)
  if (~.is_dir and hits_this_row) {
    // A directory changes only its own subtree on a toggle.
    if (not is_open and children == null) { children = directory_entries(entry_path) }
    is_open = not is_open
  } else if (not ~.is_dir) {
    if (~.selected_path != entry_path) { reset_document_scroll(evt.target) }
    emit("file_select", {file_path:entry_path, name:~.name, extension:~.extension,
      target:evt.target,
      text_width_px:(if (is_pgf_document(~.extension)) pgf_text_width_px(evt.target) else null)})
  }
}

view <document_pane> {
  if (~.file == null) {
    <main class:"document-panel"
    , <div id:"empty-preview", class:"empty-preview"
      , <div class:"empty-preview-icon", "▤">
        <h1 "Open a file">
        <p "Choose a file from the project tree to inspect it.">
        <p class:"empty-preview-note", "CSV and TSV open as resizable tables. JSON, YAML, TOML, INI, ICS, and VCF open as property trees. XML opens as a node tree or with its declared stylesheet. Email shows its headers and body. Markdown, HTML, RTF, LaTeX, PDF, diagrams, and images open as rendered documents; other files open as source.">
      >
    >
  } else if (is_renderable_document(~.file["extension"])) {
    <main class:"document-panel"
    , <div class:"document-header"
      , <div class:"document-title"
        , <div class:"document-path", ~.file["file_path"]>
          <h1 class:"document-name", ~.file["name"]>
        >
        <span class:"preview-kind rendered", if (~.preview_mode == "view") "View" else "Source">
      >
      <div class:"document-content"
      , if (~.preview_mode == "view") {
        if (property_format(~.file["extension"]) == "xml" and
            ~.file["xml_stylesheet"]) {
          // The file loader resolves the PI's href and applies its CSS in this frame.
          <iframe id:"xml-preview", class:"document-preview",
            src:absolute_file_path(~.file["file_path"])>
        } else if (property_format(~.file["extension"]) != null) {
          apply(<property_inspector file:~.file, parsed:~.property_data,
            filter_text:~.property_filter, open_paths:~.property_open_paths,
            closed_paths:~.property_closed_paths, more_paths:~.property_more_paths>)
        } else if (is_eml_document(~.file["extension"])) {
          apply(<eml_preview message:~.eml_data>)
        } else if (is_table_document(~.file["extension"])) {
          apply(<csv_preview rows:~.csv_data, widths:~.csv_widths,
            visible_rows:~.csv_visible_rows>)
        } else if (is_image_document(~.file["extension"])) {
          let preview = selected_preview(~.file);
          <section id:"image-preview", class:"rendered-preview image-preview", apply(preview)>
        } else if (is_pdf_document(~.file["extension"])) {
          let preview = selected_preview(~.file);
          <section id:"pdf-preview", class:"rendered-preview pdf-preview",
            apply(preview)>
        } else if (document_format(~.file["extension"]) == "rtf") {
          let preview = selected_preview(~.file);
          <section id:"rtf-preview", class:"rendered-preview rtf-preview",
            apply(preview)>
        } else if (document_format(~.file["extension"]) == "html") {
          // defer optional document transforms until their file is selected.
          <iframe id:"html-preview", class:"document-preview",
            src:absolute_file_path(~.file["file_path"])>
        } else {
          let preview = selected_preview(~.file);
          <section id:"rendered-preview", class:"rendered-preview",
            apply(preview)>
        }
      } else {
        let source = selected_source(~.file);
        <section class:"source-tab-panel"
        , <pre id:"source-preview", class:"source-preview", source>
        >
      }
      >
      <nav class:"document-tabs"
      , <button class:(if (~.preview_mode == "view") "document-tab tab-view active" else "document-tab tab-view"), "View">
        if (not is_raster_document(~.file["extension"])) {
          <button class:(if (~.preview_mode == "source") "document-tab tab-source active" else "document-tab tab-source"), "Source">
        }
      >
    >
  } else {
    let source = selected_source(~.file);
    <main class:"document-panel"
    , <div class:"document-header"
      , <div class:"document-title"
        , <div class:"document-path", ~.file["file_path"]>
          <h1 class:"document-name", ~.file["name"]>
        >
        <span class:"preview-kind source", "Source">
      >
      <section class:"source-tab-panel"
      , <pre id:"source-preview", class:"source-preview", source>
      >
    >
  }
}
on click(evt) {
  let target_class = evt["target_class"]
  if (contains(target_class, "tab-view")) { emit("preview_tab", "view") }
  else if (contains(target_class, "tab-source")) { emit("preview_tab", "source") }
}

// --------------------------------------------------------------------------
// Project browser application
// --------------------------------------------------------------------------

edit <project_tree> state root_open: true, open_paths: [], filter_text: "", selected_path: "" {
  let matching_root_entries = [for (entry in PROJECT_ENTRIES where entry_matches_filter(entry, filter_text)) entry];

  <aside class:"file-panel"
    , <div class:"file-panel-header"
      , <div class:"project-title"
        , <span class:"project-icon", "⌘">
          <span "lambda">
        >
        <div class:"tree-filter-wrap"
        , <input id:"file-filter", type:"search", class:"tree-filter", value:filter_text,
            placeholder:"Filter file names">
          if (filter_text != "") {
            <button class:"tree-filter-clear", title:"Clear file name filter", "×">
          }
        >
      >
      <div id:"project-tree", class:"project-tree"
      , <div class:"tree-row project-root"
        , <button class:"root-toggle",
            if (root_open) "▾" else "▸">
          <span class:"tree-icon folder-icon", "▣">
          <span class:"tree-label", "project root">
        >
        if (root_open) {
          <div class:"tree-children"
          , for (entry in matching_root_entries)
              apply(<tree_entry
                parent_path:PROJECT_ROOT,
                name:entry.name,
                extension:entry.extension,
                is_dir:entry.is_dir,
                depth:1,
                initial_open:path_is_open(open_paths,
                                          child_path(PROJECT_ROOT, entry.name)),
                open_paths:open_paths,
                selected_path:selected_path,
                filter_text:filter_text
              >)
          >
        }
      >
    <div class:"file-panel-footer", if (filter_text == "") "Project root: ." else "Filtering file names">
  >
}
on click(evt) {
  let target_class = evt["target_class"]
  let parent_class = evt["target_parent_class"]
  if (contains(target_class, "tree-row project-root") or
      contains(parent_class, "tree-row project-root")) {
    if (root_open) { open_paths = visible_open_paths(evt.target) }
    root_open = not root_open
  } else if (target_class == "tree-filter-clear") {
    if (root_open) { open_paths = visible_open_paths(evt.target) }
    filter_text = ""
  }
}
on input(evt) {
  let character = evt.char
  if (evt.target_class == "tree-filter" and character != null and character != "") {
    if (root_open) { open_paths = visible_open_paths(evt.target) }
    let caret_pos = evt.caret_pos
    filter_text = slice(filter_text, 0, caret_pos) ++ character ++
      slice(filter_text, caret_pos, len(filter_text))
  }
}
on keydown(evt) {
  if (evt.target_class == "tree-filter") {
    let next = if (evt.key == "Backspace") { erase_backwards(filter_text, evt) }
      else if (evt.key == "Delete") { erase_forwards(filter_text, evt) }
      else if (evt.key == "Escape") { "" }
      else { null }
    if (next == null) { return }
    if (root_open) { open_paths = visible_open_paths(evt.target) }
    filter_text = next
    // The model owns this edit; a second native edit would desynchronize it.
    return 'prevent-default'
  }
}
on file_select(entry) {
  // Row-local toggles do not rebuild the tree; snapshot open rows only when
  // selection will rebuild it for the selected highlight.
  open_paths = visible_open_paths(entry["target"])
  selected_path = entry["file_path"]
  emit("document_select", entry)
}

// The stable model keeps the tree's state when document controls update.
let PROJECT_TREE_MODEL = <project_tree>

edit <doc_editor_app> state selected_file: null, preview_mode: "view", property_filter: "", property_data: null, property_open_paths: [], property_closed_paths: [], property_more_paths: [], eml_data: null, csv_data: null, csv_widths: [], csv_visible_rows: CSV_PAGE_SIZE, csv_resize: null {
  <div class:"doc-editor"
  , apply(PROJECT_TREE_MODEL, {mode: "edit"})
  apply(<document_pane file:selected_file, preview_mode:preview_mode,
    property_filter:property_filter, property_data:property_data,
    property_open_paths:property_open_paths, property_closed_paths:property_closed_paths,
    property_more_paths:property_more_paths, eml_data:eml_data,
    csv_data:csv_data, csv_widths:csv_widths, csv_visible_rows:csv_visible_rows>)
  >
}
on click(evt) {
  let target_class = evt["target_class"]
  if (target_class == "property-filter-clear") {
    property_filter = ""
    property_more_paths = []
  }
}
on input(evt) {
  let character = evt.char
  if (evt.target_class == "property-filter" and
      character != null and character != "") {
    let caret_pos = evt.caret_pos
    property_filter = slice(property_filter, 0, caret_pos) ++ character ++
      slice(property_filter, caret_pos, len(property_filter))
    property_closed_paths = []
    property_more_paths = []
  }
}
on keydown(evt) {
  if (evt.target_class == "property-filter") {
    let next = if (evt.key == "Backspace") { erase_backwards(property_filter, evt) }
      else if (evt.key == "Delete") { erase_forwards(property_filter, evt) }
      else if (evt.key == "Escape") { "" }
      else { null }
    if (next == null) { return }
    property_filter = next
    property_closed_paths = []
    property_more_paths = []
    // The model owns this edit; a second native edit would desynchronize it.
    return 'prevent-default'
  }
}
on document_select(entry) {
  let format = property_format(entry["extension"])
  // Retain the parsed tree while the user edits the filter or expands nodes.
  let parsed = if (format == null) null else input(entry["file_path"], format) ^ { null }
  selected_file = {file_path: entry["file_path"], name: entry["name"],
    extension: entry["extension"], text_width_px: entry["text_width_px"],
    xml_stylesheet: (if (format == "xml") xml_has_stylesheet(parsed) else false)}
  property_data = if (format == "properties") properties_tree(parsed) else parsed
  eml_data = if (is_eml_document(entry["extension"])) input(entry["file_path"], 'eml') ^ { null } else null
  csv_data = if (is_table_document(entry["extension"]))
    input(entry["file_path"], lower(entry["extension"])) ^ { null } else null
  csv_widths = [for (column in csv_columns(csv_data)) CSV_DEFAULT_WIDTH]
  csv_visible_rows = CSV_PAGE_SIZE
  csv_resize = null
  preview_mode = "view"
  property_filter = ""
  property_open_paths = property_initial_open_paths(property_data, format)
  property_closed_paths = []
  property_more_paths = []
}
on preview_tab(tab) {
  preview_mode = tab
}
on property_toggle(entry) {
  let path = entry["path"]
  if (property_filter == "") {
    if (entry["is_open"]) {
      property_open_paths = [for (open_path in property_open_paths where open_path != path) open_path]
    } else { property_open_paths = [*property_open_paths, path] }
  } else {
    if (entry["is_open"]) { property_closed_paths = [*property_closed_paths, path] }
    else { property_closed_paths = [for (closed_path in property_closed_paths where closed_path != path) closed_path] }
  }
}
on property_more(path) {
  property_more_paths = [*property_more_paths, path]
}
on csv_resize_start(gesture) { csv_resize = gesture }
on csv_more_rows(_) { csv_visible_rows = csv_visible_rows + CSV_PAGE_SIZE }
on csv_resize_move(x) {
  if (csv_resize != null) { csv_widths = csv_resized_widths(csv_widths, csv_resize, x) }
}
// A release can land over the file panel after shrinking a wide column.
on mouseup(evt) {
  if (csv_resize == null) { return }
  csv_widths = csv_resized_widths(csv_widths, csv_resize, evt.x)
  csv_resize = null
}

// --------------------------------------------------------------------------
// Page shell
// --------------------------------------------------------------------------

<html lang:"en",
  <head
    <meta charset:"UTF-8">
    <title "Lambda Document Viewer">
    // the math stylesheet selects KaTeX symbol fonts; register their bundled faces.
    <link rel:"stylesheet", href:"../../lmd/package/math/katex.css">
    <style pdf_html.DEFAULT_CSS>
    <style latex_css.STYLESHEET>
    <style math_css.get_stylesheet(null)>
    <style "
      * { box-sizing: border-box; }
      body { margin: 0; height: 100vh; overflow: hidden; background: #eef1f5; color: #20242c;
             font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; }
      .doc-editor { display: flex; height: 100vh; overflow: hidden; }

      .file-panel { width: 300px; min-width: 240px; max-width: 40vw; display: flex;
                    min-height: 0; flex-direction: column; background: #1f2430; color: #d7dce5;
                    border-right: 1px solid #343c4d; }
      .file-panel-header { padding: 18px 14px 12px; border-bottom: 1px solid #343c4d; }
      .project-title { display: flex; align-items: center; gap: 8px; margin: 0 2px 14px;
                       font-size: 14px; font-weight: 700; letter-spacing: 0.02em; color: #f4f6fa; }
      .project-icon { color: #8eb5ff; font-size: 17px; }
      .tree-filter-wrap { position: relative; }
      .tree-filter { width: 100%; height: 34px; border: 1px solid #465166; border-radius: 6px;
                     background: #292f3d; color: #f5f7fb; padding: 0 36px 0 10px; outline: none; }
      .tree-filter:focus { border-color: #79a6ff; box-shadow: 0 0 0 2px rgba(121,166,255,.2); }
      .tree-filter::placeholder { color: #9aa5b7; }
      .tree-filter-clear { position: absolute; top: 4px; right: 4px; width: 26px; height: 26px;
                           padding: 0; border: 0; border-radius: 4px; background: transparent;
                           color: #aeb9ca; cursor: pointer; font-size: 19px; line-height: 26px; }
      .tree-filter-clear:hover { background: #3b4557; color: #fff; }
      .project-tree { min-height: 0; flex: 1; overflow: auto; padding: 8px 6px 16px; }
      /* Keep long row labels scrollable without recursively measuring every
         expanded descendant for each ancestor's max-content width. */
      .tree-entry, .tree-children { min-width: 0; }
      .tree-row { min-height: 28px; display: flex; align-items: center; padding-right: 8px;
                  border-radius: 5px; cursor: pointer; color: #c6cedb; user-select: none; }
      .tree-row:hover { background-color: #2c3444; color: #fff; }
      .tree-row.selected { background: #365383; color: #fff; }
      .project-root { padding-left: 2px; color: #f5f7fb; font-weight: 650; }
      .tree-toggle, .root-toggle { width: 22px; height: 24px; padding: 0; border: 0; background: transparent;
                                   color: #aeb9ca; cursor: pointer; font-size: 14px; }
      .tree-toggle:hover, .root-toggle:hover { color: #fff; }
      .tree-spacer { width: 22px; height: 24px; }
      .tree-icon { width: 18px; margin-right: 4px; text-align: center; font-size: 13px; }
      /* Keep the indentation and icon fixed when a file name overflows. */
      .tree-toggle, .root-toggle, .tree-spacer, .tree-icon { flex-shrink: 0; }
      .folder-icon { color: #e5bb62; }
      @font-face { font-family: 'Seti Icons'; src: url('icons/seti.woff') format('woff'); }
      .file-icon { height: 20px; line-height: 20px; color: #9bbdfc; }
      .default-icon { font-size: 13px; }
      .lambda-icon { font-size: 19px; }
      .seti-icon { font-family: 'Seti Icons'; font-size: 19px; font-weight: normal; }
      /* Hit-tested text spans need their own cursor value in the file tree. */
      .tree-label, .tree-icon, .tree-spacer { cursor: pointer; }
      .tree-label { white-space: nowrap; font-size: 13px; }
      .file-panel-footer { padding: 10px 14px; border-top: 1px solid #343c4d; color: #99a5b7;
                           font-size: 11px; }

      .document-panel { min-width: 0; min-height: 0; flex: 1; display: flex; flex-direction: column; background: #fff; }
      /* A definite pane keeps long documents out of the column flex container's
         intrinsic-size pass while preserving the preview's own scrolling. */
      .document-content { position: relative; min-width: 0; min-height: 0; flex: 1; overflow: hidden; }
      .document-content > .rendered-preview, .document-content > .source-tab-panel,
      .document-content > .document-preview { position: absolute; top: 0; right: 0; bottom: 0; left: 0; }
      .document-header { min-height: 77px; display: flex; align-items: center; justify-content: space-between;
                         gap: 18px; padding: 15px 28px; border-bottom: 1px solid #e2e6ec; background: #fbfcfe; }
      .document-path { margin-bottom: 3px; color: #7b8798; font-size: 12px; font-family: 'SF Mono', Menlo, monospace; }
      .document-name { margin: 0; color: #212936; font-size: 18px; font-weight: 650; }
      .preview-kind { flex: 0 0 auto; padding: 4px 9px; border-radius: 999px; font-size: 12px; font-weight: 600; }
      .preview-kind.rendered { background: #e3f4eb; color: #1b7447; }
      .preview-kind.source { background: #e8edf5; color: #536275; }

      .empty-preview { max-width: 520px; margin: auto; padding: 48px 36px; text-align: center; color: #596779; }
      .empty-preview-icon { color: #89a9df; font-size: 42px; }
      .empty-preview h1 { margin: 14px 0 7px; color: #2b3545; font-size: 24px; }
      .empty-preview p { margin: 5px 0; line-height: 1.5; }
      .empty-preview-note { margin-top: 18px !important; color: #8491a1; font-size: 13px; }
      .source-tab-panel { min-height: 0; flex: 1; overflow: auto; background: #fcfcfd; }
      .source-preview { min-height: 100%; margin: 0; padding: 26px 30px;
                        color: #293545; font: 13px/1.55 'SF Mono', Menlo, Consolas, monospace; white-space: pre; }
      .rendered-preview { min-height: 0; flex: 1; overflow: auto; padding: 30px clamp(24px, 6vw, 80px) 60px; }
      .rtf-preview { background: #eef1f5; }
      .rtf-preview .document-body { box-sizing: border-box; min-height: 100%; padding: 42px 52px;
                                    background: #fff; box-shadow: 0 2px 14px #d9dee7; }
      .rtf-preview .document-body p { margin: 0 0 .85em; white-space: pre-wrap; }
      .property-preview { padding: 0 0 40px; background: #fff; color: #263448;
                          font: 13px/1.4 'SF Mono', Menlo, Consolas, monospace; }
      .property-filter-bar { position: sticky; top: 0; z-index: 1; height: 44px; display: flex;
                             align-items: center; gap: 7px; padding: 0 16px; border-bottom: 1px solid #dce2eb;
                             background: #fbfcfe; }
      .property-filter-icon { color: #8b98a8; font-size: 21px; line-height: 1; }
      .property-filter { min-width: 0; flex: 1; height: 32px; border: 0; outline: none;
                         background: transparent; color: #263448; font: inherit; }
      .property-filter::placeholder { color: #8b98a8; }
      .property-filter-clear { border: 0; background: transparent; color: #77869a;
                               cursor: pointer; font-size: 20px; }
      .property-row { display: flex; min-height: 30px; align-items: baseline; gap: 8px;
                      padding: 5px 18px 5px 0; border-bottom: 1px solid #f0f2f5; }
      .property-expandable { cursor: pointer; }
      .property-entry:nth-child(even) > .property-row { background: #f7f9fb; }
      .property-row:hover { background: #edf4ff !important; }
      .property-toggle, .property-spacer { flex: 0 0 16px; width: 16px; height: 18px;
                                           padding: 0; border: 0; background: none; color: #7b8798;
                                           font: 13px/18px sans-serif; text-align: center; }
      .property-toggle { cursor: pointer; }
      .property-toggle:hover { color: #185da8; }
      .property-name { flex: 0 0 38%; min-width: 0; overflow-wrap: anywhere; color: #1268c0; }
      .property-value { min-width: 0; flex: 1; overflow-wrap: anywhere; white-space: pre-wrap;
                        color: #ab2778; }
      .property-summary { color: #6b788b; font-style: italic; }
      .property-message { padding: 18px 22px; color: #6b788b; }
      .property-root-value { padding-left: 32px; }
      .eml-preview { background: #fff; color: #263448; }
      .eml-message { max-width: 900px; min-width: 0; margin: 0 auto; }
      .eml-header-table { width: 100%; table-layout: fixed; border-collapse: collapse;
                          font: 13px/1.45 'SF Mono', Menlo, Consolas, monospace; }
      .eml-header-table tr:nth-child(even) { background: #f7f9fb; }
      .eml-header-table th, .eml-header-table td { padding: 7px 10px; border-bottom: 1px solid #e8edf3;
                                                   text-align: left; vertical-align: top; overflow-wrap: anywhere; }
      .eml-header-table th { width: 30%; color: #1268c0; font-weight: 600; }
      .eml-header-table td { color: #263448; white-space: pre-wrap; }
      .eml-body { box-sizing: border-box; width: 100%; margin: 22px 0 0;
                  color: #263448; font: 13px/1.55 'SF Mono', Menlo, Consolas, monospace;
                  white-space: pre-wrap; overflow-wrap: anywhere; }
      .eml-html-body { margin-top: 22px; overflow-wrap: anywhere; }
      .property-more { display: block; width: 100%; padding: 9px 20px; border: 0;
                       background: #f3f7fc; color: #195fa8; text-align: left;
                       font: 600 12px/18px sans-serif; cursor: pointer; }
      .property-more:hover { background: #e7f0fb; }
      .csv-preview { padding: 0 0 32px; background: #fff; }
      .csv-empty { padding: 22px; color: #68778a; }
      .csv-table { table-layout: fixed; border-collapse: separate; border-spacing: 0;
                   font: 13px/1.4 'SF Mono', Menlo, Consolas, monospace; color: #263448; }
      .csv-table th, .csv-table td { box-sizing: border-box; max-width: 0; padding: 9px 14px;
                                     overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
                                     border-right: 1px solid #e5e9ef; border-bottom: 1px solid #edf0f4; }
      .csv-table th { position: sticky; top: 0; z-index: 1; padding-right: 20px;
                      background: #f0f5fb; color: #294e78; font-weight: 650; text-align: left; }
      .csv-table tbody tr:nth-child(even) { background: #f8fafc; }
      .csv-table tbody tr:hover { background: #edf5ff; }
      .csv-header-label { display: block; overflow: hidden; text-overflow: ellipsis; }
      .csv-resize { position: absolute; top: 0; right: 0; width: 10px; height: 37px;
                    box-sizing: border-box; border-left: 2px solid #c5d5e8;
                    cursor: col-resize; user-select: none; }
      .csv-resize:hover { background: #d2e5fb; border-left-color: #4588d0; }
      .csv-more { display: block; width: 100%; padding: 10px 18px; border: 0;
                  background: #f3f7fc; color: #195fa8; text-align: left;
                  font: 600 12px/18px sans-serif; cursor: pointer; }
      .csv-more:hover { background: #e7f0fb; }
      .image-preview { display: flex; align-items: center; justify-content: center; }
      .image-preview img { display: block; max-width: 100%; max-height: 100%; object-fit: contain; }
      /* an iframe is replaced: inset:0 alone keeps its 150px intrinsic height (CSS 2.1 §10.6.5) */
      .document-preview { min-width: 0; min-height: 0; flex: 1; width: 100%; height: 100%; border: 0; display: block; background: #fff; }
      .document-tabs { flex: 0 0 auto; display: flex; gap: 2px; justify-content: flex-end;
                       padding: 7px 18px; border-top: 1px solid #e2e6ec; background: #fbfcfe; }
      .document-tab { padding: 5px 11px; border: 0; border-radius: 5px; background: transparent;
                      color: #68778a; cursor: pointer; font-size: 12px; font-weight: 650; }
      .document-tab:hover { background-color: #e8edf5; color: #31425a; }
      .document-tab.active { background: #dce9fd; color: #1c5da6; }
      .document-body, .latex-output { max-width: 900px; margin: 0 auto; color: #232a35; font-size: 16px; line-height: 1.65; }
      .document-body h1, .document-body h2, .document-body h3, .document-body h4,
      .latex-output h1, .latex-output h2, .latex-output h3, .latex-output h4 { color: #192130; line-height: 1.25; }
      .document-body h1, .latex-output h1 { margin: .2em 0 .65em; font-size: 2em; }
      .document-body h2, .latex-output h2 { margin: 1.45em 0 .55em; font-size: 1.55em; }
      .document-body h3, .latex-output h3 { margin: 1.3em 0 .45em; font-size: 1.25em; }
      /* Email is read inside the editor pane, so use a tighter type scale than full documents. */
      .eml-html-body .document-body { font-size: 14px; line-height: 1.55; }
      .eml-html-body .document-body h1 { margin: .8em 0 .45em; font-size: 21px; }
      .eml-html-body .document-body h2 { margin: 1.2em 0 .4em; font-size: 17px; }
      .eml-html-body .document-body h3 { margin: 1em 0 .35em; font-size: 15px; }
      .document-body p, .latex-output p { margin: .75em 0; }
      .document-body ul, .document-body ol, .latex-output ul, .latex-output ol { padding-left: 1.5em; }
      .document-body blockquote, .latex-output blockquote { margin: 1em 0; padding: .15em 1em;
                     border-left: 4px solid #a6badc; color: #4c5c70; background: #f6f9fd; }
      .rst-contents { margin: 1.4em 0; padding: 1em 1.4em; background: #f7f9fc;
                      border-left: 3px solid #adc2de; }
      .rst-contents-title { margin: 0 0 .5em; font-weight: 650; }
      .rst-contents ul { margin: 0; padding-left: 1.5em; }
      .rst-directive { margin: 1em 0; padding: .5em 1em; border-left: 3px solid #b6c5db; background: #f8fafd; }
      .rst-directive-title { font-weight: 650; }
      .rst-warning, .rst-danger, .rst-error, .rst-caution { border-left-color: #d89566; background: #fff9f4; }
      .document-body dl { margin: 1em 0; }
      .document-body dt { margin-top: .6em; font-weight: 650; }
      .document-body dd { margin-left: 1.4em; }
      .document-body code, .latex-output code { padding: 2px 5px; border-radius: 4px; background: #eef1f5;
                     font: .9em 'SF Mono', Menlo, monospace; }
      .document-body pre, .latex-output pre { overflow: auto; padding: 13px 15px; border-radius: 6px;
                     background: #f1f3f6; color: #243042; white-space: pre-wrap; }
      .document-body pre code, .latex-output pre code { padding: 0; background: transparent; color: inherit; }
      .document-body a, .latex-output a { color: #1769c2; }
      .document-body table, .latex-output table { width: 100%; border-collapse: collapse; margin: 1em 0; }
      .document-body caption { text-align: left; font-weight: 650; margin-bottom: .35em; }
      .document-body th, .document-body td, .latex-output th, .latex-output td { border: 1px solid #dbe1ea; padding: 7px 9px; text-align: left; }
      .document-body th, .latex-output th { background: #f3f6fa; }
      .document-body img, .latex-output img { max-width: 100%; }
      .preview-error { color: #b14444; }
      @media (max-width: 700px) {
        .file-panel { width: 210px; min-width: 180px; }
        .document-header { padding: 13px 16px; }
        .rendered-preview, .source-preview { padding-left: 18px; padding-right: 18px; }
      }
    ">
  >
  <body
    apply(<doc_editor_app>, {mode: "edit"})
  >
>
