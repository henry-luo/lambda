// rte_prototype.ls — Stage 2 Rich Text Editor prototype
//
// Pure Lambda Script reactive UI. No JavaScript. The toolbar dispatches via
// `view <toolbar_button>` `on click(evt)` handlers; the surrounding
// `edit <rte_app>` template owns the editor state. Markdown is parsed once
// into a Mark tree at script start and serialised back to markdown only on
// Save (button or Cmd+S handler) — never per keystroke.
//
// Run:
//   ./lambda.exe view test/ui/rte_prototype.ls
// Headless smoke:
//   ./lambda.exe view test/ui/rte_prototype.ls \
//     --event-file test/ui/rte_prototype.json --headless --no-log
//
// Status: S2.1 scaffold — parses the source markdown, renders the toolbar,
// dispatches commands via emit/on, and exposes #status / #markdown-output
// for end-to-end smoke. Per-tag `view <h1>` / `view <p>` / `view <strong>`
// templates and the full `edit_open` -> `edit_exec` wiring through
// `lambda.editor.mod_editor` land in S2.2.

import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_md_schema
import lambda.editor.mod_source_pos
import dom

let SOURCE_PATH = './test/input/simple.md'
let initial_doc = input(SOURCE_PATH, 'markdown') ^ { null }
let initial_body = initial_doc[0]

fn maybe_attr(name, value) => if (value == null) { [] } else { [{name: name, value: value}] }
fn attrs_concat(a, b) => [*a, *b]

fn editor_attrs(item) {
  if (type(item) != element) { [] }
  else if (name(item) == 'a') { attrs_concat(maybe_attr('href', item.href), maybe_attr('title', item.title)) }
  else if (name(item) == 'img') { attrs_concat(maybe_attr('src', item.src), maybe_attr('alt', item.alt)) }
  else { [] }
}

fn mark_to_editor(item) {
  if (type(item) == string) { text(item) }
  else if (type(item) == element) {
    // Element indexing includes its optional value slot; semantic model
    // children come from content() so absent slots never become text "null".
    let kids = [for (child in content(item)) mark_to_editor(child)]
    let tag = if (name(item) == 'code' and len(kids) == 1 and is_text(kids[0]) and kids[0].text.contains("\n")) { 'code_block' } else { name(item) }
    node_attrs(tag, editor_attrs(item), kids)
  }
  else { text(string(item)) }
}

fn attr_value_at(attrs, key, i, n) {
  if (i >= n) { null }
  else if (attrs[i].name == key) { attrs[i].value }
  else { attr_value_at(attrs, key, i + 1, n) }
}
fn attr_value(node, key) => attr_value_at(node.attrs, key, 0, len(node.attrs))

fn text_has_mark_at(marks, mark, i, n) {
  if (i >= n) { false }
  // marks are {name,value} records; command-applied marks must render by name.
  else if (marks[i].name == mark) { true }
  else { text_has_mark_at(marks, mark, i + 1, n) }
}
fn text_has_mark(marks, mark) => text_has_mark_at(marks, mark, 0, len(marks))

fn render_text_leaf(leaf) {
  let content0 = if (leaf.text == "") { "\u200B" } else { leaf.text }
  let content1 = if (text_has_mark(leaf.marks, 'code')) { <code content0> } else { content0 }
  let content2 = if (text_has_mark(leaf.marks, 'u')) { <u content1> } else { content1 }
  let content3 = if (text_has_mark(leaf.marks, 'em')) { <em content2> } else { content2 }
  if (text_has_mark(leaf.marks, 'strong')) { <strong content3> } else { content3 }
}

let initial_editor_doc = node('doc', [for (child in initial_body) mark_to_editor(child)])
let initial_title_len = len(doc_text(node_at(initial_editor_doc, [0])))
let initial_selection = text_selection(pos([0, 0], initial_title_len), pos([0, 0], initial_title_len))
let initial_editor = edit_open(initial_editor_doc, html5_subset_schema, initial_selection)

let toolbar_commands = [
  {button: "btn-bold", input_type: "formatBold", payload: {}},
  {button: "btn-italic", input_type: "formatItalic", payload: {}},
  {button: "btn-underline", input_type: "formatUnderline", payload: {}},
  {button: "btn-ul", input_type: "insertUnorderedList", payload: {kind: 'bullet'}},
  {button: "btn-ol", input_type: "insertOrderedList", payload: {kind: 'ordered'}},
  {button: "btn-quote", input_type: "formatBlockquote", payload: {}},
  {button: "btn-code", input_type: "insertCodeBlock", payload: {data: ""}},
  {button: "btn-link", input_type: "insertLink",
   payload: {href: "https://example.com", title: "", label: "Example"}},
  {button: "btn-image", input_type: "insertImage",
   payload: {src: "https://example.com/image.png", alt: "Example image"}},
  {button: "btn-table", input_type: "insertTable", payload: {rows: 2, cols: 2, header: true}},
  {button: "btn-undo", input_type: "historyUndo", payload: {}},
  {button: "btn-redo", input_type: "historyRedo", payload: {}}
]

fn toolbar_command_at(button, index) {
  if (index >= len(toolbar_commands)) null
  else if (toolbar_commands[index].button == button) toolbar_commands[index]
  else toolbar_command_at(button, index + 1)
}

fn toolbar_command(button) => toolbar_command_at(button, 0)

// ============================================================================
// Per-tag render templates — markdown Mark tree -> HTML
// ============================================================================
// The markdown parser emits HTML-compatible tags (<h1>, <p>, <ul>, <li>,
// <strong>, <em>, <a>, <code>, <blockquote>, ...). For each tag we declare a
// Child results are explicitly applied and spread because the current element
// grammar uses commas for the attribute/content boundary (S16.9.3).
//
// A catch-all `view any { ~ }` lets unknown inline scalar variants fall
// through unchanged (the markdown parser produces some compound string-like
// values that Radiant's DOM builder otherwise rejects).

view any { ~ }

view map {
  if (~.kind == 'text') { render_text_leaf(~) }
  else if (~.kind == 'node' and ~.tag == 'doc') { <div class:"doc-body", *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h1') { <h1 *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h2') { <h2 *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h3') { <h3 *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h4') { <h4 *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h5') { <h5 *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h6') { <h6 *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'p' or ~.tag == 'paragraph')) { <p *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'span') { <span *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'strong') { <strong *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'em') { <em *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'u') { <u *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'code_block' or ~.tag == 'pre')) { <pre *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'code') { <code *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'ul' or ~.tag == 'list')) { <ul *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'ol') { <ol *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'li') { <li *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'blockquote') { <blockquote *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'a') { <a href:attr_value(~, 'href'), *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'img') { <img src:attr_value(~, 'src'), alt:attr_value(~, 'alt')> }
  else if (~.kind == 'node' and ~.tag == 'hr') { <hr> }
  else if (~.kind == 'node' and ~.tag == 'table') { <table *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'thead') { <thead *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'tbody') { <tbody *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'tfoot') { <tfoot *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'tr') { <tr *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'th') { <th *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'td') { <td *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node') { <div *[for (c in ~.content) apply(c)]> }
  else { "" }
}

view <h1> { <h1 *[for (c in ~) apply(c)]> }
view <h2> { <h2 *[for (c in ~) apply(c)]> }
view <h3> { <h3 *[for (c in ~) apply(c)]> }
view <h4> { <h4 *[for (c in ~) apply(c)]> }
view <h5> { <h5 *[for (c in ~) apply(c)]> }
view <h6> { <h6 *[for (c in ~) apply(c)]> }
view <p> { <p *[for (c in ~) apply(c)]> }
view <span> { <span *[for (c in ~) apply(c)]> }
view <strong> { <strong *[for (c in ~) apply(c)]> }
view <em> { <em *[for (c in ~) apply(c)]> }
view <u> { <u *[for (c in ~) apply(c)]> }
view <code> { <code *[for (c in ~) apply(c)]> }
view <a> { <a href:~.href, *[for (c in ~) apply(c)]> }
view <ul> { <ul *[for (c in ~) apply(c)]> }
view <ol> { <ol *[for (c in ~) apply(c)]> }
view <li> { <li *[for (c in ~) apply(c)]> }
view <blockquote> { <blockquote *[for (c in ~) apply(c)]> }
view <table> { <table *[for (c in ~) apply(c)]> }
view <thead> { <thead *[for (c in ~) apply(c)]> }
view <tbody> { <tbody *[for (c in ~) apply(c)]> }
view <tfoot> { <tfoot *[for (c in ~) apply(c)]> }
view <tr> { <tr *[for (c in ~) apply(c)]> }
view <th> { <th *[for (c in ~) apply(c)]> }
view <td> { <td *[for (c in ~) apply(c)]> }
view <hr> { <hr> }
view <img> { <img src:~.src, alt:~.alt> }
view <body> { <div class:"doc-body", *[for (c in ~) apply(c)]> }

// ============================================================================
// Toolbar — every button is one `view <toolbar_button>` instance whose
// click handler emits an `rte_cmd` event up to the surrounding `edit <rte_app>`.
// ============================================================================

view <toolbar_button> {
  <button class:("toolbar-btn " ++ ~.cmd), ~.label>
}
on click() {
  emit("rte_cmd", ~.cmd)
}

// ============================================================================
// Top-level reactive editor application
// ============================================================================

edit <rte_app> state editor: initial_editor, status: ("opened:" ++ SOURCE_PATH), markdown_output: "" {
  <div class:"rte-app",
    <div id:"toolbar", class:"toolbar",
      apply(<toolbar_button cmd:"btn-bold",      label:"B">)
      apply(<toolbar_button cmd:"btn-italic",    label:"I">)
      apply(<toolbar_button cmd:"btn-underline", label:"U">)
      apply(<toolbar_button cmd:"btn-ul",        label:"UL">)
      apply(<toolbar_button cmd:"btn-ol",        label:"OL">)
      apply(<toolbar_button cmd:"btn-quote",     label:"Q">)
      apply(<toolbar_button cmd:"btn-code",      label:"Code">)
      apply(<toolbar_button cmd:"btn-link",      label:"Link">)
      apply(<toolbar_button cmd:"btn-image",     label:"Img">)
      apply(<toolbar_button cmd:"btn-table",     label:"Tbl">)
      apply(<toolbar_button cmd:"btn-undo",      label:"Undo">)
      apply(<toolbar_button cmd:"btn-redo",      label:"Redo">)
      apply(<toolbar_button cmd:"btn-save",      label:"Save">)
    >
    <div id:"doc", contenteditable:"true", tabindex:"0", class:"doc-host",
      apply(editor.doc)
    >
    <pre id:"markdown-output", markdown_output>
    <div id:"status", status>
  >
}
on beforeinput(evt) {
  return 'pass'
}
on editaction(evt) {
  let run = edit_handle_dom_action(editor, evt)
  editor = run.editor
  status = if (run.result.failure == null) evt.input_type else run.result.failure
  return run.result
}
on selectionchange(evt) {
  if (evt.source_selection != null or evt.source_pos != null) {
    if (not editor.mounted) {
      editor = edit_mount(editor, evt.target, 'html5_subset')
    }
    let accepted = edit_accept_dom_selection(editor, evt)
    if (accepted.changed) { editor = accepted.editor }
  }
}
on rte_cmd(evt) {
  let cmd = evt
  if (cmd == "btn-save") {
    markdown_output = doc_text(editor.doc)
    status = "saved"
  } else {
    let descriptor = toolbar_command(cmd)
    if (descriptor == null) { status = cmd }
    else {
      let request = edit_request_from_toolbar(descriptor.input_type, descriptor.payload)
      let run = edit_handle_request(editor, request)
      editor = run.editor
      status = if (run.result.failure == null) cmd else run.result.failure
      if (editor.surface_handle != null and
          not dom.finish_model_edit(editor.surface_handle, run.result)) {
        status = "model-completion-failed"
      }
      return null
    }
  }
}

// ============================================================================
// Page shell
// ============================================================================

<html lang:"en",
  <head
    <meta charset:"UTF-8">
    <title "Radiant Rich Text Editor — Prototype">
    <style "
      * { box-sizing: border-box; }
      body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
             background: #f5f6f8; margin: 0; padding: 24px; }
      .rte-app { max-width: 920px; margin: 0 auto; background: white;
                 border-radius: 10px; box-shadow: 0 4px 16px rgba(0,0,0,0.08);
                 overflow: hidden; }
      .toolbar { display: flex; gap: 4px; align-items: center; min-height: 52px; padding: 10px 14px;
                 background: #f8f9fb; border-bottom: 1px solid #e5e7eb; }
      .toolbar-btn { flex: 0 0 auto; min-width: 32px; height: 32px; line-height: 30px;
             padding: 0 8px; border: 1px solid #d8dadf; background: white;
             border-radius: 6px; cursor: pointer; font-size: 14px;
             text-align: center; white-space: nowrap; overflow: hidden; }
      .toolbar-btn:hover { background: #eef0f3; }
      .toolbar-btn.active { background: #d8e5ff; border-color: #6b8ce0; }
      .doc-host { min-height: 420px; padding: 24px 30px; outline: none;
                  font-size: 16px; line-height: 1.55; color: #1f2328; }
      .doc-host h1 { font-size: 1.9em; margin: 0.4em 0 0.5em; }
      .doc-host h2 { font-size: 1.5em; margin: 1em 0 0.5em; }
      .doc-host p { margin: 0.5em 0; }
      .doc-host ul, .doc-host ol { padding-left: 1.4em; margin: 0.5em 0; }
      .doc-host blockquote { border-left: 4px solid #cbd5e1;
                             margin: 0.6em 0; padding: 0.2em 0.9em;
                             color: #475569; }
      .doc-host code { background: #f1f3f5; padding: 1px 5px; border-radius: 3px;
                       font-family: 'SF Mono', Menlo, monospace; font-size: 0.92em; }
      .doc-host pre { background: #f1f3f5; padding: 8px 10px; border-radius: 4px;
              font-family: 'SF Mono', Menlo, monospace; font-size: 0.92em;
              line-height: 1.45; white-space: pre-wrap; margin: 0.7em 0; }
      .doc-host a { color: #1d4ed8; text-decoration: underline; }
      .doc-host table { border-collapse: collapse; margin: 0.8em 0; width: 100%; }
      .doc-host th, .doc-host td { border: 1px solid #cbd5e1; padding: 6px 8px; text-align: left; }
      .doc-host th { background: #f8fafc; font-weight: 650; }
      .doc-host img { max-width: 100%; border: 1px solid #d8dadf; border-radius: 4px; }
      #markdown-output { display: none; }
      #status { padding: 8px 14px; background: #f1f3f5; color: #555;
                font-size: 12px; border-top: 1px solid #e5e7eb; font-family: monospace; }
    ">
  >
  <body
    apply(<rte_app>, {mode: "edit"})
  >
>
