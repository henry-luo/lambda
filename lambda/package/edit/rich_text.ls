// rich_text.ls — the rich-text editing surface shared by the Markdown and
// HTML profiles (vibe/radiant/Radiant_Design_Edit_Mode.md §5).
//
// The surface is a projection of the lambda.editor model: every model node is
// rendered through apply(), so the render map records its source path and the
// native selection bridge maps DOM positions back to model positions
// (render_map_record_source_path). The projection is never read back as the
// document; export always starts from the model.

import lambda.editor.mod_doc
import lambda.editor.mod_step
import .model

// An empty leaf still needs a caret box: a zero-width space holds the line.
fn leaf_text(leaf) => if (leaf.text == "") "​" else leaf.text

// Marks, innermost first, as the surface nests them around a leaf. The
// document's own mark attributes (a span's class) are not projected.
let leaf_marks = ['code', 'sub', 'sup', 'del', 's', 'u', 'span', 'em', 'i', 'strong', 'b']

fn mark_element(mark, child) {
  if (mark == 'code') <code child>
  else if (mark == 'sub') <sub child>
  else if (mark == 'sup') <sup child>
  else if (mark == 'del' or mark == 's') <del child>
  else if (mark == 'u') <u child>
  else if (mark == 'em' or mark == 'i') <em child>
  else if (mark == 'strong' or mark == 'b') <strong child>
  else <span child>
}

fn wrap_leaf(marks, i, child) =>
  if (i >= len(leaf_marks)) child
  else wrap_leaf(marks, i + 1, if (has_mark(marks, leaf_marks[i])) mark_element(leaf_marks[i], child) else child)

fn render_text_leaf(leaf) => wrap_leaf(leaf.marks, 0, leaf_text(leaf))

fn align_style(n) {
  let align = attr_get(n, 'align')
  if (align == null) null else "text-align: " ++ align
}

// A short label for retained source: its tag, first attribute, and text.
fn source_label(source) {
  if (type(source) == array) join([for (x in source) source_label(x)], " ")
  else if (type(source) == string) trim(source)
  else if (type(source) != element) ""
  else if (name(source) == '#comment') "<!--" ++ string(source.data) ++ "-->"
  else {
    let first_attr = join(take([for (k, v at map(source)) " " ++ string(k) ++ "=\"" ++ string(v) ++ "\""], 1), "")
    let excerpt = slice(trim(plain_text(source)), 0, 60)
    "<" ++ string(name(source)) ++ first_attr ++ ">" ++ (if (excerpt == "") "" else " " ++ excerpt)
  }
}

// Kept source reads as written: Markdown keeps its text, HTML its parsed tree.
fn kept_source(n) {
  let text = attr_get(n, 'html')
  if (text != null) text else source_label(attr_get(n, 'source'))
}

// Source that the editor keeps but does not edit shows as a read-only chip.
fn opaque_inline(cls, title, source) =>
  <span class: "edit-opaque " ++ cls, contenteditable: "false", title: title, source>

fn opaque_block(cls, title, source) =>
  <div class: "edit-opaque edit-opaque-block " ++ cls, contenteditable: "false", title: title, source>

fn render_list_item(n, kids) {
  let checked = attr_get(n, 'checked')
  if (checked == null) <li *kids>
  else <li class: if (checked) "edit-task edit-task-done" else "edit-task", *kids>
}

// Flow containers the HTML profile opens for editing.
let flow_tags = ['div', 'section', 'article', 'main', 'header', 'footer', 'nav', 'aside']

// Links in the surface select and edit content; they never navigate the
// session away, so the projection carries no href (proposal §5 HTML).
fn render_node(n, kids) {
  let tag = n.tag
  if (tag == 'doc') <div class: "edit-doc", *kids>
  // a paragraph the HTML source did not spell is its container's own text
  else if (tag == 'p' and attr_get(n, 'edit implied') == true) <p class: "edit-implied", *kids>
  else if (tag == 'p') <p *kids>
  else if (tag == 'h1') <h1 *kids>
  else if (tag == 'h2') <h2 *kids>
  else if (tag == 'h3') <h3 *kids>
  else if (tag == 'h4') <h4 *kids>
  else if (tag == 'h5') <h5 *kids>
  else if (tag == 'h6') <h6 *kids>
  else if (tag == 'blockquote') <blockquote *kids>
  else if (member(flow_tags, tag)) <div class: "edit-flow", *kids>
  else if (tag == 'figure') <figure *kids>
  else if (tag == 'figcaption') <figcaption *kids>
  else if (tag == 'ul') <ul *kids>
  else if (tag == 'ol') <ol start: attr_get(n, 'start'), *kids>
  else if (tag == 'li') render_list_item(n, kids)
  else if (tag == 'pre' or tag == 'code_block') <pre class: "edit-code", *kids>
  else if (tag == 'hr') <hr>
  else if (tag == 'br') <br>
  else if (tag == 'a') <a class: "edit-link", title: attr_get(n, 'href'), *kids>
  else if (tag == 'img') <img src: attr_get(n, 'src'), alt: attr_get(n, 'alt'), title: attr_get(n, 'title')>
  else if (tag == 'table') <table *kids>
  else if (tag == 'thead') <thead *kids>
  else if (tag == 'tbody') <tbody *kids>
  else if (tag == 'tfoot') <tfoot *kids>
  else if (tag == 'tr') <tr *kids>
  else if (tag == 'th') <th style: align_style(n), *kids>
  else if (tag == 'td') <td style: align_style(n), *kids>
  else if (tag == 'strong' or tag == 'b') <strong *kids>
  else if (tag == 'em' or tag == 'i') <em *kids>
  else if (tag == 'u') <u *kids>
  else if (tag == 'del' or tag == 's') <del *kids>
  else if (tag == 'sub') <sub *kids>
  else if (tag == 'sup') <sup *kids>
  else if (tag == 'code') <code *kids>
  else if (tag == 'span') <span *kids>
  else if (tag == 'raw_html') opaque_inline("edit-raw", "Raw HTML (kept as written)", kept_source(n))
  else if (tag == 'math') opaque_inline("edit-math", "Math (kept as written)", "$" ++ attr_get(n, 'tex') ++ "$")
  else if (tag == 'html_block') opaque_block("edit-raw", "Raw HTML (kept as written)", kept_source(n))
  else if (tag == 'math_block') opaque_block("edit-math", "Math (kept as written)", "$$" ++ attr_get(n, 'tex') ++ "$$")
  else if (tag == 'opaque') opaque_block("edit-raw", "Kept as written: " ++ string(attr_get(n, 'label')), attr_get(n, 'label'))
  else <div *kids>
}

// Model nodes and text leaves are maps; the edit package applies no other maps.
view map {
  if (~.kind == 'text') render_text_leaf(~)
  else if (~.kind == 'node') render_node(~, [for (c in ~.content) apply(c)])
  else ""
}

// The editable host. Its content is the applied model document.
pub fn surface(editor, css_class) =>
  <div id: "edit-surface", class: "edit-surface " ++ css_class, contenteditable: "true", tabindex: "0",
    apply(editor.doc)
  >

// Authoring styles for the surface content (proposal §5: the document's own
// page styles are not applied while editing).
pub let css = "
  .edit-surface { outline: none; min-height: 100%; padding: 28px 36px 48px;
                  font-size: 16px; line-height: 1.6; color: #1f2328; }
  .edit-surface h1 { font-size: 2em; margin: 0.4em 0 0.5em; padding-bottom: 0.2em;
                     border-bottom: 1px solid #d8dee4; }
  .edit-surface h2 { font-size: 1.5em; margin: 1em 0 0.5em; padding-bottom: 0.2em;
                     border-bottom: 1px solid #d8dee4; }
  .edit-surface h3 { font-size: 1.25em; margin: 1em 0 0.4em; }
  .edit-surface h4, .edit-surface h5, .edit-surface h6 { font-size: 1em; margin: 1em 0 0.4em; }
  .edit-surface p { margin: 0.5em 0; }
  .edit-surface p.edit-implied { margin: 0; }
  .edit-surface figure { margin: 0.8em 0; }
  .edit-surface figcaption { font-size: 0.9em; color: #57606a; }
  .edit-surface ul, .edit-surface ol { padding-left: 1.6em; margin: 0.5em 0; }
  .edit-surface li.edit-task { list-style: none; }
  .edit-surface li.edit-task::before { content: '[ ] '; font-family: monospace; color: #57606a; }
  .edit-surface li.edit-task-done::before { content: '[x] '; }
  .edit-surface blockquote { border-left: 4px solid #d0d7de; margin: 0.6em 0;
                             padding: 0.1em 1em; color: #57606a; }
  .edit-surface code { background: #eff1f3; padding: 1px 5px; border-radius: 4px;
                       font-family: 'SF Mono', Menlo, monospace; font-size: 0.9em; }
  .edit-surface pre { background: #f6f8fa; padding: 10px 12px; border-radius: 6px;
                      font-family: 'SF Mono', Menlo, monospace; font-size: 0.9em;
                      line-height: 1.45; white-space: pre-wrap; margin: 0.8em 0; }
  .edit-surface a.edit-link { color: #0969da; text-decoration: underline; cursor: text; }
  .edit-surface table { border-collapse: collapse; margin: 0.8em 0; }
  .edit-surface th, .edit-surface td { border: 1px solid #d0d7de; padding: 6px 12px; }
  .edit-surface th { background: #f6f8fa; font-weight: 600; }
  .edit-surface img { max-width: 100%; }
  .edit-surface hr { border: none; border-top: 2px solid #d8dee4; margin: 1.2em 0; }
  .edit-opaque { background: #fff8c5; border: 1px dashed #d4a72c; border-radius: 4px;
                 color: #6e5600; font-family: 'SF Mono', Menlo, monospace; font-size: 0.85em;
                 padding: 0 4px; white-space: pre-wrap; }
  .edit-opaque-block { display: block; margin: 0.6em 0; padding: 6px 8px; }
"
