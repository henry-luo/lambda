// doc_viewer.ls — bundled lambda.doc document viewer (D9.3.4)
//
// A read-only project browser built with Lambda's reactive UI. The left
// panel browses local directories or captured archive trees; children load on demand.
//
// Run: ./lambda.exe view
// With the startup splash: ./lambda.exe demo
// Headless smoke: ./lambda.exe view --headless --no-log

import pdf: lambda.doc.pdf.pdf
import dom
import pdf_html: lambda.doc.pdf.html
import latex: lambda.doc.latex.latex
import latex_css: lambda.doc.latex.css
import math_renderer: lambda.doc.math.math
import graph_doc: lambda.graph.document
import tikz: lambda.doc.tikz.tikz
import media: .media
import rtf: .rtf
import docx_view: .docx.view
import docx_css: .docx.css
import pptx: .pptx
import pptx_view: .pptx.view
import xlsx: .xlsx
import xlsx_view: .xlsx.preview
import slide_html: lambda.slide.html
import ebook: .ebook
import ebook_reader: .ebook.reader
import xml: .xml
import chrome: .document_chrome
import paths: lambda.io.path
import editor: lambda.editor.app
import editor_session: lambda.editor.session
import source_editor: lambda.editor.surface.source
import publish: lambda.ui.core.publish

let PROJECT_ROOT = "."
// Transformed browsers retain the source directory as their URL, so bundle the icon font.
let ICON_FONT = media.data_uri(input(sys.lambda.home ++ "/package/doc/icons/seti.woff", 'binary')^, "font/woff")^

// --------------------------------------------------------------------------
// Filesystem and selection helpers
// --------------------------------------------------------------------------

fn archive_extension(extension) => contains(
  ["zip", "jar", "war", "odt", "ods", "odp", "7z", "tar", "rar", "gz", "tgz", "bz2", "tbz2", "xz", "txz", "zst", "tzst"], lower(extension)) ^ { false }
fn archive_input_format(extension) => if (extension == "7z") '7z'
  else if (extension == "rar") 'rar'
  else if (contains(["tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz", "zst", "tzst"], extension)) 'tar'
  else 'zip'
fn entry_is_container(entry) any^ => entry.is_dir == true or (archive_extension(entry.extension) or false)
fn directory_entries(source) any^ {
  let loaded = if (type(source) == element) {
    if (source.is_dir or source.entry_path == "") source else input(source, archive_input_format(lower(source.extension)))^
  } else { input(source)^ }
  let entries = if (type(loaded) == array) loaded else content(loaded)^;
  // The directory reader sorts names; keep that order within each group.
  [for (entry in entries where ((entry_is_container(entry)) ^ { false })) entry] ++
    [for (entry in entries where ((not entry_is_container(entry)) ^ { false })) entry]
}
fn child_path(parent_path, child_name) => join([parent_path, child_name], "/")
fn absolute_file_path(path) any^ => if (starts_with(path, "/") or path[1] == ":") path
  else sys.proc.self.cwd ++ "/" ++ path

fn path_is_open(open_paths, path) => contains(open_paths, path) ^ { false }
fn tree_hit_class(path) any^ => "tree-hit-" ++ replace(replace(path, "/", "_"), ".", "_")
// An empty-space hit targets the row; absent event classes recover to false (S7.6).
fn event_hits_row(evt, hit_class) =>
  (contains(evt["target_class"], hit_class ++ " ") ^ { false }) or
  (contains(evt["target_parent_class"], hit_class ++ " ") ^ { false })

// A tree-wide redraw needs the expansion currently owned by individual rows.
fn visible_open_paths(target) {
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

fn text_before_input(value, evt) any^ {
  let selection_start = evt["selection_start"]
  let caret_pos = evt["caret_pos"]
  // Optional event offsets can be undefined; only integers are valid slice indices.
  if (type(selection_start) == int) { bounded_text_offset(value, selection_start) }
  else if (type(caret_pos) == int) { bounded_text_offset(value, caret_pos) }
  else { len(value) }
}

fn text_after_input(value, evt) any^ {
  let selection_end = evt["selection_end"]
  if (type(selection_end) == int) { bounded_text_offset(value, selection_end) }
  else { text_before_input(value, evt) }
}

fn erase_backwards(value, evt) any^ {
  let start = text_before_input(value, evt)
  let end = text_after_input(value, evt)
  if (start != end) { slice(value, 0, start) ++ slice(value, end, len(value)) }
  else if (start > 0) { slice(value, 0, start - 1) ++ slice(value, start, len(value)) }
  else { value }
}

fn erase_forwards(value, evt) any^ {
  let start = text_before_input(value, evt)
  let end = text_after_input(value, evt)
  if (start != end) { slice(value, 0, start) ++ slice(value, end, len(value)) }
  else if (start < len(value)) { slice(value, 0, start) ++ slice(value, start + 1, len(value)) }
  else { value }
}

fn entry_is_visible(entry, parent_path) any^ {
  // archive members retain dotfiles and build-like names as part of the package.
  if (type(entry) == element) { true }
  else {
    // Directory nodes expose metadata via member access (S2.4.3v4).
    let entry_name = lower(entry.name)
    // Hide implementation artefacts without preventing normal source browsing:
    // dot entries, build/release output directories, and *.exe binaries.
    not starts_with(entry_name, ".") and
      // The shipped POSIX executable has no extension, and browsing the viewer's
      // own UI sources recursively creates another viewer inside this preview.
      not (parent_path == "." and entry_name == "lambda") and
      not (parent_path == "./test" and entry_name == "ui") and
      (not entry.is_dir or
        (not starts_with(entry_name, "build") and not starts_with(entry_name, "release"))) and
      (entry.is_dir or not ends_with(entry_name, ".exe"))
  }
}

fn entry_matches_filter(entry, parent_path, filter_text) any^ {
  // Directories remain visible so a search result can be reached by expanding
  // its ancestry; the filter itself applies to file names.
  entry_is_visible(entry, parent_path) and
    (entry_is_container(entry) or filter_text == "" or contains(lower(entry.name), lower(filter_text)))
}

fn document_format(extension) any^ {
  let ext = lower(extension)
  if (contains(["md", "markdown", "mdown", "mkdn"], ext)) { "markdown" }
  else if (contains(["wiki", "mediawiki"], ext)) { "wiki" }
  else if (contains(["rst", "rest"], ext)) { "rst" }
  else if (ext == "org") { "org" }
  else if (contains(["adoc", "asciidoc", "asc"], ext)) { "asciidoc" }
  else if (ext == "man" or contains(["1", "2", "3", "4", "5", "6", "7", "8", "9", "1m", "3p"], ext)) { "man" }
  else if (ext == "textile") { "textile" }
  else if (ext == "rtf") { "rtf" }
  else if (contains(["htm", "html", "xht", "xhtml"], ext)) { "html" }
  else { null }
}

fn is_latex_document(extension) {
  let ext = lower(extension)
  ext == "tex" or ext == "latex"
}

fn is_pdf_document(extension) => lower(extension) == "pdf"
fn is_docx_document(extension) => lower(extension) == "docx"
fn is_pptx_document(extension) => lower(extension) == "pptx"
fn is_xlsx_document(extension) => lower(extension) == "xlsx"
fn is_office_document(extension) => is_docx_document(extension) or is_pptx_document(extension) or is_xlsx_document(extension)
fn is_epub_document(extension) => lower(extension) == "epub"
fn is_eml_document(extension) => lower(extension) == "eml"
fn is_pgf_document(extension) => lower(extension) == "pgf"
// Match the raster formats decoded by the shared image library and SVG.
fn is_image_document(extension) => contains(
  ["png", "jpg", "jpeg", "gif", "webp", "avif", "bmp", "tif", "tiff", "ico", "svg"],
  lower(extension)) ^ { false }
fn is_raster_document(extension) => lower(extension) == "psd" or
  (is_image_document(extension) and lower(extension) != "svg")
fn is_table_document(extension) => contains(["csv", "tsv"], lower(extension)) ^ { false }
fn graph_flavor(extension) {
  let ext = lower(extension)
  if (ext == "mmd") { "mermaid" }
  else if (ext == "dot" or ext == "gv") { "dot" }
  else if (ext == "d2") { "d2" }
  else if (ext == "dsl" or ext == "structurizr") { "structurizr" }
  else { null }
}

// These formats already have their own document renderer. Keep its styles
// and controls in a frame, as for an authored HTML page.
fn framed_format(file) =>
  type(file["source"]) != element and
    (contains(["eps", "epsi", "psd", "diff", "patch", "slides", "fo", "xslfo", "rpd"],
              lower(file["extension"])) ^ { false })

fn property_format(extension) any^ {
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
  let own = [for (entry in members where ((len(entry.path) == depth + 1) ^ { false })) entry]
  let deeper = [for (entry in members where ((len(entry.path) > depth + 1) ^ { false })) entry]
  if (len(deeper) == 0) { {name:name, value:own[0].value} }
  else { {name:name, value:properties_group(properties_nodes(deeper, depth + 1), own)} }
}
fn properties_nodes(entries, depth) {
  let names = unique([for (entry in entries) entry.path[depth]]);
  [for (name in names)
    properties_node(name, [for (entry in entries where entry.path[depth] == name) entry], depth)]
}
fn properties_tree(flat) any^ {
  if (type(flat) != map) { flat }
  else {
    let entries = [for (key, value in flat) {path:split(string(key), "."), value:value}]
    properties_group(properties_nodes(entries, 0), [])
  }
}

fn is_renderable_document(extension) any^ =>
  document_format(extension) != null or is_latex_document(extension) or
    is_pdf_document(extension) or is_office_document(extension) or is_epub_document(extension) or is_eml_document(extension) or
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

fn file_icon(extension) any^ {
  let ext = if (extension == null) "" else lower(extension)
  // seti glyphs identify parsed formats; source-only files retain the plain-text icon.
  if (ext == "ls") { "λ" }
  else if (ext == "json") { SETI_JSON }
  else if (contains(["yaml", "yml"], ext)) { SETI_YAML }
  else if (contains(["toml", "ini", "properties", "props"], ext)) { SETI_CONFIG }
  else if (is_table_document(ext) or is_xlsx_document(ext)) { SETI_CSV }
  else if (ext == "xml") { SETI_XML }
  else if (contains(["db", "sqlite", "sqlite3"], ext)) { SETI_DB }
  else if (contains(["md", "markdown", "mdown", "mkdn", "mdx", "wiki", "mediawiki",
                     "rst", "rest", "org", "adoc", "asciidoc", "asc", "textile", "txtl",
                     "mark", "typ", "typst"], ext) or
           ext == "man" or contains(["1", "2", "3", "4", "5", "6", "7", "8", "9", "1m", "3p"], ext)) { SETI_MARKDOWN }
  else if (contains(["htm", "html"], ext)) { SETI_HTML }
  else if (ext == "rtf" or is_docx_document(ext)) { SETI_WORD }
  else if (is_latex_document(ext)) { SETI_TEX }
  else if (is_pdf_document(ext)) { SETI_PDF }
  else if (is_pgf_document(ext)) { SETI_SVG }
  else if (contains(["mmd", "dot", "gv", "d2", "dsl", "structurizr"], ext)) { SETI_PIPELINE }
  else if (ext == "svg") { SETI_SVG }
  else if (is_image_document(ext)) { SETI_IMAGE }
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

fn selected_source(file) any^ {
  if (file == null) { "" }
  else {
    let source = file["source"]
    let parsed = if (type(source) == element) input(source) ^ { null } else null;
    if (type(parsed) == binary) string(parsed)
    else input(source, 'text') ^ { "Unable to read selected file: " ++ ^.message }
  }
}

fn selected_image_source(file) any^ {
  if (type(file["source"]) != element) { absolute_file_path(file["file_path"]) }
  else {
    let extension = lower(file["extension"])
    let mime = if (extension == "svg") "image/svg+xml"
      else if (contains(["jpg", "jpeg"], extension)) "image/jpeg"
      else if (contains(["tif", "tiff"], extension)) "image/tiff"
      else if (extension == "ico") "image/x-icon"
      else "image/" ++ extension
    media.data_uri(input(file["source"], 'binary')^, mime)^
  }
}

fn selected_office(file) map^ {
  let source = file["source"]
  // An Office member needs its own ZIP root; local filesystem entries use their path.
  let document = if (type(source) != element) file["file_path"]
    else if (source.entry_path == "") source else input(source, 'zip')^;
  if (is_pptx_document(file["extension"])) pptx.convert(document)^
  else if (is_xlsx_document(file["extension"])) xlsx.convert(document)^
  else docx_view.convert_preview(document,null,true)^
}

pn selected_epub(file) map^ {
  let snapshot = input(file["source"], 'zip')^;
  return ebook.parse(snapshot)^
}

fn xml_has_stylesheet(parsed) {
  if (parsed == null) { false }
  else {
    // The parser preserves the stylesheet processing instruction as a document child.
    any([for (child in ((content(parsed)) ^ { [] }) where ((type(child) == element and
      string(name(child)) == "?xml-stylesheet" and len(content(child)) > 0) ^ { false }))
      contains(content(child)[0], "href=")])
  }
}

fn xml_is_docbook(parsed) any^ {
  let root = xml.root(parsed);
  if (not (root is element)) false else
    xml.expanded(name(root),xml.bindings(root)).uri == "http://docbook.org/ns/docbook"
}

fn selected_preview(file) any^ {
  if (file == null) { null }
  else {
    let selected_path = file["source"]
    let format = document_format(file["extension"])
    let flavor = graph_flavor(file["extension"])
    if (is_image_document(file["extension"])) {
      <img src:selected_image_source(file), alt:file["name"]>
    }
    else if (is_pdf_document(file["extension"])) {
      // Render in this document's runtime; a nested PDF iframe cannot start another runtime.
      let parsed = input(selected_path, 'pdf') ^ { null }
      if (parsed == null) { <p class:"preview-error", "Unable to read selected PDF"> }
      else { pdf.pdf_to_html(parsed, null) ^ { <p class:"preview-error", "Unable to render selected PDF"> } }
    }
    else if (is_latex_document(file["extension"])) {
      // Parse through the package so the TeX engine sees its adapter registry (D10.2.2);
      // the selected file's path also resolves local packages, bibliographies and images.
      let parsed = latex.parse_file(selected_path)
      if (parsed == null) { <p class:"preview-error", "Unable to read selected LaTeX"> }
      else { latex.render(parsed, {source_path:absolute_file_path(file["file_path"])}) ^ {
        <p class:"preview-error", "Unable to render selected LaTeX"> } }
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
    else if (format == "rtf") {
      rtf.parse(selected_source(file)^) ^ { <p class:"preview-error", "Unable to render selected RTF"> }
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
    let child_rows = [for (index, child in ((content(value)) ^ { [] }))
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

fn property_is_container(value) any^ =>
  type(value) == map or type(value) == array or
    (type(value) == element and
      (len(map(value)) > 0 or any([for (child in ((content(value)) ^ { [] })) type(child) == element])))
fn property_roots(parsed, format) {
  if (format == "xml") {
    // The XML reader wraps the document and its processing instructions in <document>.
    [for (index, child in ((content(parsed)) ^ { [] })
      where ((type(child) == element and
        not starts_with(string(name(child)), "?") and
        not starts_with(string(name(child)), "!")) ^ { false }))
      {name:string(name(child)), value:child, segment:string(index)}]
  }
  else if (format == "ics" and type(parsed) == array) {
    [for (index, calendar in parsed) {name:"VCALENDAR", value:calendar, segment:string(index)}]
  }
  else if (format == "ics") { [{name:"VCALENDAR", value:parsed, segment:"calendar"}] }
  else if (format == "vcf" and type(parsed["contacts"]) != array) {
    [{name:"VCARD", value:parsed, segment:"card"}]
  } else { property_children(parsed, format) }
}
fn property_initial_open_paths(parsed, format) any^ {
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
fn property_hit_class(path) any^ =>
  "property-hit-" ++ replace(replace(replace(path, "/", "_"), ".", "_"), " ", "_")

fn property_matches(value, name, filter_text, format) any^ {
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

fn property_summary(value, format) any^ {
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
  else if (type(value) == map) { chrome.collection_summary('object', len(value)) }
  else if (type(value) == array) { chrome.collection_summary('array', len(value)) }
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

fn rendered_children(item) => [for (child in ((content(item)) ^ { [] })) apply(child)]

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
view <footnotes> { <section class:"footnotes", *[rendered_children(~)]> }
view <footnote> {
  let label = (if (~.number != null) ~.number else ~.label) ++ ". ";
  let marker = <span class:"footnote-number", label>;
  <div class:"footnote", id:~.id, *[[marker] ++ rendered_children(~)]>
}
view <u> { <u *[rendered_children(~)]> }
view <code> {
  if (~.type == "block" and ~.language == "mermaid") {
    let source = if (len(content(~)) > 0) content(~)[0] else ""
    graph_doc.from_mermaid(source) ^ { <pre class:"diagram-error", <code source>> }
  }
  else if (~.type == "block") { <pre <code *[rendered_children(~)]>> }
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
view <img> { <img src:~.src, alt:~.alt, class:~.class, style:~.style, ["data-emoji"]:~["data-emoji"]> }
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
  if ((evt.button != 0 or not contains(evt.target_class, "csv-resize"))^) { return }
  let column_id = dom.get_attribute(evt.target, "data-csv-column")
  let indices = [for (index, column in csv_columns(~.rows)^ where (string(index) == column_id)^) index]
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
    [for (child in ((property_children(~.value, ~.format)) ^ { [] })
      where ((property_matches(child.value, child.name, ~.filter_text, ~.format)) ^ { false })) child]
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
        where ((property_matches(child.value, child.name, query, format)) ^ { false })) child]
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
  let roots = [for (child in ((content(body)) ^ { [] }) where ((type(child) == element and name(child) == 'html') ^ { false })) child]
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
    let current_children = if (children == null) { directory_entries(~.source) ^ { [] } } else { children };
    if (type(current_children) != array) [] else
      [for (child in current_children where ((entry_matches_filter(child, entry_path, ~.filter_text)) ^ { false })) child]
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
            is_dir:entry_is_container(child),
            source:child,
            depth:(~.depth + 1),
            initial_open:path_is_open(~.open_paths,
                                      child_path(entry_path, child.name)),
            open_paths:~.open_paths,
            selected_path:~.selected_path,
            filter_text:~.filter_text
          >)
      >
      if (type(children) == string) { <p class:"tree-load-error", children> }
    }
  >
}
on click(evt) {
  let entry_path = child_path(~.parent_path, ~.name)
  let hit_class = tree_hit_class(entry_path)
  let hits_this_row = event_hits_row(evt, hit_class)
  if (~.is_dir and hits_this_row) {
    // A directory changes only its own subtree on a toggle.
    if (not is_open and children == null) { children = directory_entries(~.source) ^ { ^.message } }
    is_open = not is_open
  } else if (not ~.is_dir) {
    if ((~.selected_path != entry_path)^) { reset_document_scroll(evt.target)^ }
    emit("file_select", {file_path:entry_path, source:~.source, name:~.name, extension:~.extension,
      target:evt.target,
      text_width_px:(if (is_pgf_document(~.extension)) pgf_text_width_px(evt.target) else null)})
  }
}

pn update_preview_scrollbars(panel) {
  let preview = dom.query_selector(panel, ".rendered-preview")
  if (preview == null) { return }
  let metrics = dom.scroll_metrics(preview)
  // Radiant's auto tracks appear on hover. Keep the viewer's overflowing axes visible.
  publish.style(preview, "overflow-x", if (metrics.scroll_width > metrics.client_width) "scroll" else "auto")^
  publish.style(preview, "overflow-y", if (metrics.scroll_height > metrics.client_height) "scroll" else "auto")^
}

fn document_mode_labels(extension, renderable, editable) =>
  [for (caption in [if (renderable) "View" else null,
    if (editable) "Edit" else null,
    if (not renderable or not is_raster_document(extension)) "Source" else null] where caption != null) caption]

fn document_modes(extension, mode, renderable, editable) {
  if (is_xlsx_document(extension)) chrome.badge("View", "rendered")
  else if (not renderable and not editable) chrome.badge("Source", "source")
  else chrome.mode_tabs([for (caption in document_mode_labels(extension, renderable, editable))
    if (not renderable and caption == "Source") chrome.badge("Source", "source")
    else chrome.mode_button(caption, lower(caption) == mode)])
}

// Pane-local controls are state on a pane model the app rebuilds only for a
// new file or a CSV change, so a property toggle, tab switch or property
// filter edit re-renders this pane alone instead of the whole project tree.
view <document_pane> () any^ state preview_mode: ~.initial_preview_mode or "view", property_filter: "", property_open_paths: ~.initial_open_paths, property_closed_paths: [], property_more_paths: [], edit_error: null {
  if (~.file == null) {
    <main class:"document-panel"
    , <div id:"empty-preview", class:"empty-preview"
      , <div class:"empty-preview-icon", "▤">
        <h1 "Open a file">
        <p "Choose a file from the project tree to inspect it.">
        <p class:"empty-preview-note", "CSV and TSV open as resizable tables, and XLSX opens as a workbook. JSON, YAML, TOML, INI, ICS, and VCF open as property trees. XML opens as a node tree or with its declared stylesheet. Email shows its headers and body. EPUB books, Word DOCX, PowerPoint PPTX, Markdown, HTML, RTF, LaTeX, PDF, diagrams, and images open as rendered documents; other files open as source.">
      >
    >
  } else if (is_renderable_document(~.file["extension"]) or framed_format(~.file)) {
    <main class:"document-panel"
    , chrome.header(~.file["file_path"], ~.file["name"],
        document_modes(~.file["extension"], preview_mode, true, type(~.file.source) != element));
      <div class:"document-content"
      , if (preview_mode == "view") {
        if (property_format(~.file["extension"]) == "xml" and
            ~.file["xml_rendered"]) {
          // The XML loader selects DocBook presentation or the declared CSS in this frame.
          <iframe id:"xml-preview", class:"document-preview",
            src:absolute_file_path(~.file["file_path"])>
        } else if (property_format(~.file["extension"]) != null) {
          apply(<property_inspector file:~.file, parsed:~.property_data,
            filter_text:property_filter, open_paths:property_open_paths,
            closed_paths:property_closed_paths, more_paths:property_more_paths>)
        } else if (is_eml_document(~.file["extension"])) {
          apply(<eml_preview message:~.eml_data>)
        } else if (is_table_document(~.file["extension"])) {
          apply(<csv_preview rows:~.csv_data, widths:~.csv_widths,
            visible_rows:~.csv_visible_rows>)
        } else if (is_image_document(~.file["extension"])) {
          let preview = selected_preview(~.file);
          <section id:"image-preview", class:"rendered-preview image-preview", apply(preview)>
        } else if (is_docx_document(~.file["extension"])) {
          <section id:"docx-preview", class:"rendered-preview docx-preview",
            apply(docx_view.preview(~.office_data))>
        } else if (is_pptx_document(~.file["extension"])) {
          <section id:"pptx-preview", class:"rendered-preview pptx-preview",apply(pptx_view.preview(~.office_data))>
        } else if (is_xlsx_document(~.file["extension"])) {
          xlsx_view.preview(~.office_data)
        } else if (is_epub_document(~.file["extension"])) {
          <section id:"epub-preview", class:"epub-preview",
            if (not (~.file.ebook.error is null)) <p class:"preview-error", ~.file.ebook.error>
            else ebook_reader.reader(~.file.ebook)^>
        } else if (is_pdf_document(~.file["extension"])) {
          let preview = selected_preview(~.file);
          <section id:"pdf-preview", class:"rendered-preview pdf-preview",
            // keep the renderer's page and text-run attributes through the preview.
            if (name(preview) == 'html') {
              <div class:"document-body", *[for (body in content(preview)^ where name(body)^ == 'body') *content(body)^]>
            } else preview>
        } else if (document_format(~.file["extension"]) == "rtf") {
          let preview = selected_preview(~.file);
          <section id:"rtf-preview", class:"rendered-preview rtf-preview",
            apply(preview)>
        } else if (document_format(~.file["extension"]) == "html") {
          // defer optional document transforms until their file is selected.
          if (type(~.file["source"]) == element) {
            <iframe id:"html-preview", class:"document-preview", srcdoc:selected_source(~.file)>
          } else {
            <iframe id:"html-preview", class:"document-preview",
              src:absolute_file_path(~.file["file_path"])>
          }
        } else if (framed_format(~.file)) {
          <iframe class:"document-preview", src:absolute_file_path(~.file["file_path"]), title:~.file["name"]>
        } else {
          let preview = selected_preview(~.file);
          <section id:"rendered-preview", class:"rendered-preview",
            apply(preview)>
        }
      } else {
        // ZIP bytes are not document source; expose the converter's escaped HTML instead.
        let source = if (is_office_document(~.file["extension"])) {
          if (not (~.office_data.error is null)) ~.office_data.error else ~.office_data.html
        } else if (is_epub_document(~.file["extension"])) {
          if (not (~.file.ebook.error is null)) ~.file.ebook.error else parse(~.file.ebook.resources[~.file.ebook.package], 'text')^
        } else selected_source(~.file);
        // Office source is generated HTML; EPUB exposes its package markup.
        let path = if (is_office_document(~.file.extension) or is_epub_document(~.file.extension))
          ~.file.file_path ++ ".html" else ~.file.file_path;
        <section class:"source-tab-panel", source_editor.readonly(path, source)>
      }
      >
      if (edit_error != null) <div class:"preview-error", role:"alert", edit_error> else null
    >
  } else {
    let source = selected_source(~.file);
    <main class:"document-panel"
    , chrome.header(~.file["file_path"], ~.file["name"],
        document_modes(~.file["extension"], "source", false, type(~.file.source) != element));
      <section class:"source-tab-panel", source_editor.readonly(~.file.file_path, source)>
      if (edit_error != null) <div class:"preview-error", role:"alert", edit_error> else null
    >
  }
}
on render(evt) {
  if (evt.event_phase == 2) {
    dom.watch_window(evt.target, "resize", true)
    update_preview_scrollbars(evt.target)^
  }
  return 'pass'
}
on resize(evt) {
  update_preview_scrollbars(evt.target)^
  return 'pass'
}
on click(evt) {
  let target_class = evt["target_class"]
  if (contains(target_class, "tab-view")) { preview_mode = "view" }
  else if (contains(target_class, "tab-source")) { preview_mode = "source" }
  else if (contains(target_class, "tab-edit")) {
    let opened = editor.open_state(~.file.file_path, {quiet_open:true}) ^ { {error: ^.message} }
    if (opened.error != null) { edit_error = "Cannot edit: " ++ opened.error; return }
    dom.set_window_title(evt.target, editor_session.window_title(opened.session, false)^)
    let extension = ~.file["extension"]
    let renderable = is_renderable_document(extension) or framed_format(~.file)
    emit("viewer_edit", {*:opened, modes:document_mode_labels(extension, renderable, true)})
  }
  else if (target_class == "property-filter-clear") {
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
    if ((next == null)^) { return }
    property_filter = next
    property_closed_paths = []
    property_more_paths = []
    // The model owns this edit; a second native edit would desynchronize it.
    return 'prevent-default'
  }
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

// --------------------------------------------------------------------------
// Project browser application
// --------------------------------------------------------------------------

edit <project_tree> state root_open: true, open_paths: [], filter_text: "", selected_path: "" {
  let matching_root_entries = [for (entry in ~.entries where ((entry_matches_filter(entry, ~.root_path, filter_text)) ^ { false })) entry];

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
          <span class:"tree-label", ~.root_name>
        >
        if (root_open) {
          <div class:"tree-children"
          , for (entry in matching_root_entries)
              apply(<tree_entry
                parent_path:~.root_path,
                name:entry.name,
                extension:entry.extension,
                is_dir:entry_is_container(entry),
                source:entry,
                depth:1,
                initial_open:path_is_open(open_paths,
                                          child_path(~.root_path, entry.name)),
                open_paths:open_paths,
                selected_path:selected_path,
                filter_text:filter_text
              >)
          >
        }
      >
    <div class:"file-panel-footer", if (filter_text == "") ~.root_path else "Filtering file names">
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
    if ((next == null)^) { return }
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
  emit("document_select", entry)
}
on viewer_selected(evt) { selected_path = evt.detail }

fn document_pane_model(file, property_data, open_paths, eml_data, office_data, csv_data, csv_widths, csv_visible_rows, preview_mode = null) =>
  <document_pane file:file, property_data:property_data, initial_open_paths:open_paths,
    eml_data:eml_data, office_data:office_data, csv_data:csv_data, csv_widths:csv_widths, csv_visible_rows:csv_visible_rows,
    initial_preview_mode:preview_mode>

// Capture the format's model once for both direct opens and tree selections.
pn load_pane(entry) element^ {
  let format = property_format(entry["extension"])
  let parsed = if (format == null) null else input(entry["source"], format) ^ { null }
  let publication = if (is_epub_document(entry["extension"]))
    selected_epub(entry) ^ { {error:^.message} } else null
  let file = {file_path:entry["file_path"], source:entry["source"], name:entry["name"],
    extension:entry["extension"], text_width_px:entry["text_width_px"],
    ebook:publication,
    xml_rendered:(if (format == "xml" and type(entry.source) != element)
      xml_has_stylesheet(parsed) or xml_is_docbook(parsed) else false)}
  let properties = if (format == "properties") properties_tree(parsed) else parsed
  let open_paths = property_initial_open_paths(properties, format)
  let eml = if (is_eml_document(entry["extension"])) input(entry["source"], 'eml') ^ { null } else null
  let office = if (is_office_document(entry["extension"]))
    selected_office(entry) ^ { {error:^.message} } else null
  let table = if (is_table_document(entry["extension"]))
    input(entry["source"], lower(entry["extension"])) ^ { null } else null
  let widths = [for (column in csv_columns(table)^) CSV_DEFAULT_WIDTH]
  return document_pane_model(file, properties, open_paths, eml, office, table, widths, CSV_PAGE_SIZE, entry.preview_mode)
}

fn table_pane(pane:element, widths, rows) element^ =>
  document_pane_model(pane.file, pane.property_data, pane.initial_open_paths,
    pane.eml_data, pane.office_data, pane.csv_data, widths, rows)

edit <doc_editor_app> state csv_resize: null, pane: ~.initial_pane, editing: null {
  <div class:"doc-editor",
    if (~.tree_model != null) apply(~.tree_model, {mode:"edit"});
    if (editing != null) <div class:"viewer-edit-host", <style editor.stylesheet> editor.surface(editing, true, editing.modes)>
    else apply(pane)
  >
}
on viewer_edit(document) { editing = document }
on viewer_preview(req) {
  let surface = dom.query_selector(dom.root_node(req.node), ".viewer-edit-host .edit-app")
  if (surface != null) {
    // Route preview through the same Save / Don't Save / Cancel flow as a file change.
    dom.dispatch(surface, {type:"editor_navigate", detail:{preview_mode:req.mode}})
  }
}
on click(evt) {
  if (editing != null) {
    let root = dom.root_node(evt.target)
    let target = if (dom.node_type(evt.target) == 1) evt.target else dom.parent_node(evt.target)
    // The file panel is outside the editor's event subtree.
    if (target == null or dom.closest(target, ".viewer-edit-host") == null) {
      let menu = dom.query_selector(root, ".viewer-edit-host .edit-table-menu,.viewer-edit-host .property-menu,.viewer-edit-host .edit-compact[open]")
      if (menu != null) {
        let surface = dom.query_selector(root, ".viewer-edit-host .edit-app")
        if (surface != null) { dom.dispatch(surface, {type:"editor_dismiss_menu"}) }
      }
    }
  }
  return 'pass'
}
on document_select(entry) {
  if (editing != null) {
    let root = dom.root_node(entry.target)
    let surface = dom.query_selector(root, ".viewer-edit-host .edit-app")
    if (surface != null) {
      // Keep the file description independent of the clicked DOM node.
      let next = {file_path:entry.file_path, source:entry.source, name:entry.name,
                  extension:entry.extension, text_width_px:entry.text_width_px}
      dom.dispatch(surface, {type:"editor_navigate", detail:next})
    }
    return
  }
  csv_resize = null
  pane = load_pane(entry)^
  let tree = dom.closest(entry.target, ".file-panel")
  if (tree != null) { dom.dispatch(tree, {type:"viewer_selected", detail:entry.file_path}) }
}
on viewer_navigation(req) {
  // Keep the current editor and its close guard if the next document fails to load.
  let mode = req.entry.preview_mode
  let entry = if (mode == null) req.entry
    else if (req.edit_path == pane.file.file_path) {*:pane.file, preview_mode:mode}
    else {*:local_file_entry(req.edit_path)^, preview_mode:mode}
  let next = load_pane(entry)^
  let root = dom.root_node(req.node)
  let tree = dom.query_selector(root, ".file-panel")
  if (tree != null) { dom.dispatch(tree, {type:"viewer_selected", detail:entry.file_path}) }
  dom.set_close_guard(req.node, false)
  dom.set_window_title(req.node, if (~.tree_model == null) entry.name else "Lambda Document Viewer")
  csv_resize = null
  pane = next
  editing = null
}
// A release can land over the file panel after shrinking a wide column.
on csv_resize_start(gesture) { csv_resize = gesture }
on csv_more_rows(_) {
  pane = table_pane(pane, pane.csv_widths, pane.csv_visible_rows + CSV_PAGE_SIZE)^
}
on csv_resize_move(x) {
  if (csv_resize != null) {
    pane = table_pane(pane, csv_resized_widths(pane.csv_widths, csv_resize, x), pane.csv_visible_rows)^
  }
}
on mouseup(evt) {
  if (csv_resize == null) { return }
  pane = table_pane(pane, csv_resized_widths(pane.csv_widths, csv_resize, evt.x), pane.csv_visible_rows)^
  csv_resize = null
}

// --------------------------------------------------------------------------
// Page shell
// --------------------------------------------------------------------------

fn viewer_document(tree_model, initial_pane = <document_pane file:null, initial_open_paths:[]>) {
<html lang:"en",
  <head
    <meta charset:"UTF-8">
    <title if (initial_pane.file != null) initial_pane.file.name else "Lambda Document Viewer">
    <style chrome.css>
    <style source_editor.surface_css>
    latex.font_stylesheet();
    <style pdf_html.DEFAULT_CSS>
    <style latex_css.STYLESHEET>
    <style docx_css.STYLESHEET>
    <style (slide_html.stage_css ++ pptx_view.css)>
    <style xlsx_view.css>
    <style (ebook_reader.stylesheet ++ ".epub-preview .ebook-reader{height:calc(100vh - 180px);min-height:400px}")>
    <style ("@font-face { font-family: 'Seti Icons'; src: url('" ++ ICON_FONT ++ "') format('woff'); }")>
    <style "
      .doc-editor, .doc-editor * { box-sizing: border-box; }
      body { margin: 0; height: 100vh; overflow: hidden; background: #eef1f5; color: #20242c;
             font-family: var(--document-font); }
      .doc-editor { display: flex; height: 100vh; overflow: hidden; }
      .viewer-edit-host { flex: 1; min-width: 0; min-height: 0; height: 100%; overflow: hidden; }
      .viewer-edit-host body.edit-app { height: 100%; min-height: 0; overflow: hidden; }
      .viewer-edit-host .edit-main { min-height: 0; overflow: auto; }

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
      .file-icon { height: 20px; line-height: 20px; color: #9bbdfc; }
      .default-icon { font-size: 13px; }
      .lambda-icon { font-size: 19px; }
      .seti-icon { font-family: 'Seti Icons'; font-size: 19px; font-weight: normal; }
      /* Hit-tested text spans need their own cursor value in the file tree. */
      .tree-label, .tree-icon, .tree-spacer { cursor: pointer; }
      .tree-label { white-space: nowrap; font-size: 13px; }
      .tree-load-error { margin: 5px 16px; color: #ffb4a8; font-size: 12px; }
      .file-panel-footer { padding: 10px 14px; border-top: 1px solid #343c4d; color: #99a5b7;
                           font-size: 11px; }

      .document-panel { min-width: 0; min-height: 0; flex: 1; display: flex; flex-direction: column; background: #fff; }
      /* A definite pane keeps long documents out of the column flex container's
         intrinsic-size pass while preserving the preview's own scrolling. */
      .document-content { position: relative; min-width: 0; min-height: 0; flex: 1; overflow: hidden; }
      .document-content > .rendered-preview, .document-content > .source-tab-panel,
      .document-content > .document-preview, .document-content > .xlsx-preview {
        position: absolute; top: 0; right: 0; bottom: 0; left: 0; }
      .document-header { min-height: 77px; display: flex; align-items: center; justify-content: space-between;
                         gap: 18px; padding: 15px 28px; border-bottom: 1px solid var(--document-rule);
                         background: var(--document-chrome); }
      .document-header .document-path { margin-bottom: 3px; }

      .empty-preview { max-width: 520px; margin: auto; padding: 48px 36px; text-align: center; color: #596779; }
      .empty-preview-icon { color: #89a9df; font-size: 42px; }
      .empty-preview h1 { margin: 14px 0 7px; color: #2b3545; font-size: 24px; }
      .empty-preview p { margin: 5px 0; line-height: 1.5; }
      .empty-preview-note { margin-top: 18px !important; color: #8491a1; font-size: 13px; }
      .source-tab-panel { min-height: 0; flex: 1; display: flex; flex-direction: column;
                          overflow: hidden; background: var(--document-source); }
      /* Give the nested source flex layout a definite height during its layout pass. */
      .source-tab-panel body.edit-format-source { height: auto; flex: 1; min-height: 0; }
      /* Keep positioned preview content below the scrollport's overflow controls. */
      .rendered-preview { min-height: 0; flex: 1; isolation: isolate; overflow: auto;
                          padding: 30px clamp(24px, 6vw, 80px) 60px; }
      .rtf-preview { background: #eef1f5; }
      .rtf-preview .document-body { box-sizing: border-box; min-height: 100%; padding: 42px 52px;
                                    background: #fff; box-shadow: 0 2px 14px #d9dee7; }
      .rtf-preview .document-body p { margin: 0 0 .85em; white-space: pre-wrap; }
      .property-preview { padding: 0 0 40px; background: #fff; color: #263448;
                          font: 13px/1.4 var(--document-mono); }
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
      .property-entry:nth-child(even) > .property-row { background: var(--document-stripe); }
      .property-row:hover { background: var(--document-hover) !important; }
      .property-toggle, .property-spacer { flex: 0 0 16px; width: 16px; height: 18px;
                                           padding: 0; border: 0; background: none; color: #7b8798;
                                           font: 13px/18px sans-serif; text-align: center; }
      .property-toggle { cursor: pointer; }
      .property-toggle:hover { color: #185da8; }
      .property-name { flex: 0 0 38%; min-width: 0; overflow-wrap: anywhere; color: var(--document-key); }
      .property-value { min-width: 0; flex: 1; overflow-wrap: anywhere; white-space: pre-wrap;
                        color: var(--document-value); }
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
      .document-body, .latex-output { max-width: 900px; margin: 0 auto; color: var(--document-prose); font-size: 16px; line-height: 1.65; }
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
      .document-body code, .latex-output code { padding: 2px 5px; border-radius: 4px; background: var(--document-code);
                     font: .9em var(--document-mono); }
      .document-body pre, .latex-output pre { overflow: auto; padding: 13px 15px; border-radius: 6px;
                     background: var(--document-code-block); color: #243042; white-space: pre-wrap; }
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
        .viewer-edit-host .edit-app .edit-document-bar { flex-wrap: wrap; row-gap: 6px; padding-inline: 12px; }
        .viewer-edit-host .edit-file { flex-basis: 100%; }
        /* Keep the action row level with Edit when Source also shows its history buttons. */
        .viewer-edit-host .edit-document-actions { gap: 8px; max-width: 100%; overflow-x: auto; }
        .rendered-preview, .source-preview { padding-left: 18px; padding-right: 18px; }
      }
      @media (max-width: 520px) {
        .document-header { flex-direction: column; align-items: flex-start; gap: 8px; }
        .document-header .document-title { max-width: 100%; }
        .document-header .document-tabs, .document-header > .preview-kind { align-self: flex-end; }
        .viewer-edit-host .edit-document-actions { gap: 4px; }
        .viewer-edit-host .edit-document-actions .edit-group:not(:last-child) { padding-right: 5px; }
        .viewer-edit-host .edit-document-actions .edit-btn-save { padding-inline: 8px; }
      }
    ">
  >
  <body
    apply(<doc_editor_app tree_model:tree_model, initial_pane:initial_pane>, {mode:"edit"})
  >
>
}

pub pn open_browser(source, options) {
  // capture once; member nodes retain this snapshot throughout the UI session (S12.4.1v2).
  let entries = directory_entries(source)^
  let segments = [for (part in split(replace(source, "\\", "/"), "/")^ where part != "") part]
  let root_name = if (len(segments) == 0) source else segments[len(segments) - 1];
  viewer_document(<project_tree root_path:source, root_name:root_name, entries:entries>)
}

fn local_file_entry(source: string) map^ {
  let name = paths.basename(replace(source, "\\", "/"))^
  let dot = last_index_of(name, ".")
  {file_path:source, source:source, name:name, extension:if (dot == null) "" else slice(name, dot + 1)}
}

pub pn open_file(source: string, options) element^ {
  let pane = load_pane(local_file_entry(source)^)^
  return viewer_document(null, pane)
}

// Keep the root identity stable across document selections.
viewer_document(<project_tree root_path:PROJECT_ROOT, root_name:"project root",
  entries:(directory_entries(PROJECT_ROOT) ^ { [] })>)
