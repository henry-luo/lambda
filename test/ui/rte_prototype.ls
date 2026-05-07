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
// `lambda.package.editor.mod_editor` land in S2.2.

import lambda.package.editor.mod_doc
import lambda.package.editor.mod_editor
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_source_pos

let SOURCE_PATH = './test/input/simple.md'
let initial_doc^err = input(SOURCE_PATH, 'markdown')
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
    let kids = [for (i in 0 to len(item) - 1) mark_to_editor(item[i])]
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
  else if (marks[i] == mark) { true }
  else { text_has_mark_at(marks, mark, i + 1, n) }
}
fn text_has_mark(marks, mark) => text_has_mark_at(marks, mark, 0, len(marks))

fn render_text_leaf(leaf) {
  let content0 = if (leaf.text == "") { "\u200B" } else { leaf.text }
  let content1 = if (text_has_mark(leaf.marks, 'code')) { <code; content0> } else { content0 }
  let content2 = if (text_has_mark(leaf.marks, 'u')) { <u; content1> } else { content1 }
  let content3 = if (text_has_mark(leaf.marks, 'em')) { <em; content2> } else { content2 }
  if (text_has_mark(leaf.marks, 'strong')) { <strong; content3> } else { content3 }
}

let initial_editor_doc = node('doc', [for (child in initial_body) mark_to_editor(child)])
let initial_title_len = len(doc_text(node_at(initial_editor_doc, [0])))
let initial_selection = text_selection(pos([0, 0], initial_title_len), pos([0, 0], initial_title_len))
let initial_editor = edit_open(initial_editor_doc, html5_subset_schema, initial_selection)

fn valid_source_pos(doc, p) {
  if (p == null) { false }
  else {
    let n = node_at(doc, p.path)
    if (n == null) { false }
    else {
      let max_offset = if (is_text(n)) { len(n.text) } else if (is_node(n)) { len(n.content) } else { -1 }
      if (max_offset < 0) { false }
      else if (p.offset < 0) { false }
      else if (max_offset < p.offset) { false }
      else { true }
    }
  }
}

fn normalize_source_pos(doc, p) {
  if (p == null) { null }
  else if (valid_source_pos(doc, p)) { p }
  else if (node_at(doc, p.path) != null and is_text(node_at(doc, p.path))) {
    let leaf = node_at(doc, p.path)
    if (p.offset < 0) { pos(p.path, 0) } else { pos(p.path, len(leaf.text)) }
  }
  else if (len(p.path) == 0) { null }
  else { normalize_source_pos(doc, pos(parent_path(p.path), p.offset)) }
}

fn valid_source_selection(doc, sel) {
  if (sel == null) { false }
  else if (sel.kind == 'text') { valid_source_pos(doc, sel.anchor) and valid_source_pos(doc, sel.head) }
  else if (sel.kind == 'node') { node_at(doc, sel.path) != null }
  else if (sel.kind == 'all') { true }
  else { false }
}

fn normalize_source_selection(doc, sel) {
  if (sel == null) { null }
  else if (sel.kind == 'text') {
    let anchor = normalize_source_pos(doc, sel.anchor)
    let head = normalize_source_pos(doc, sel.head)
    if (anchor != null and head != null) { text_selection(anchor, head) } else { null }
  }
  else if (valid_source_selection(doc, sel)) { sel }
  else { null }
}

fn event_selection_for_doc(doc, evt, fallback) {
  let sel = normalize_source_selection(doc, evt.source_selection)
  if (sel != null) { sel }
  else {
    let p = normalize_source_pos(doc, evt.source_pos)
    if (p != null) { text_selection(p, p) } else { fallback }
  }
}

fn source_selection_is_range(sel) =>
  sel != null and (sel.kind != 'text' or not pos_equal(sel.anchor, sel.head))

fn click_selection_for_doc(doc, evt, fallback) {
  let sel = normalize_source_selection(doc, evt.source_selection)
  if (source_selection_is_range(sel)) { sel }
  else {
    let p = normalize_source_pos(doc, evt.source_pos)
    if (p != null) { text_selection(p, p) }
    else if (sel != null) { sel }
    else { fallback }
  }
}

fn editor_with_event_selection(ed, evt) => edit_set_selection(ed, event_selection_for_doc(ed.doc, evt, ed.selection))
fn editor_with_click_selection(ed, evt) => edit_set_selection(ed, click_selection_for_doc(ed.doc, evt, ed.selection))

// ============================================================================
// Per-tag render templates — markdown Mark tree -> HTML
// ============================================================================
// The markdown parser emits HTML-compatible tags (<h1>, <p>, <ul>, <li>,
// <strong>, <em>, <a>, <code>, <blockquote>, ...). For each tag we declare a
// thin `view` template of the form `<tag ; apply; >`. `apply;` is the bare
// statement form (per Reactive_UI.md §8.3) which re-dispatches each child of
// `~` through the template registry, splatting them as element children.
// (`apply(target)` is the function-call form and requires an explicit target.)
//
// A catch-all `view any { ~ }` lets unknown inline scalar variants fall
// through unchanged (the markdown parser produces some compound string-like
// values that Radiant's DOM builder otherwise rejects).

view any { ~ }

view map {
  if (~.kind == 'text') { render_text_leaf(~) }
  else if (~.kind == 'node' and ~.tag == 'doc') { <div class:"doc-body"; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h1') { <h1; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h2') { <h2; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h3') { <h3; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h4') { <h4; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h5') { <h5; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'h6') { <h6; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'p' or ~.tag == 'paragraph')) { <p; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'span') { <span; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'strong') { <strong; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'em') { <em; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'u') { <u; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'code_block' or ~.tag == 'pre')) { <pre; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'code') { <code; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'ul' or ~.tag == 'list')) { <ul; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'ol') { <ol; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'li') { <li; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'blockquote') { <blockquote; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'a') { <a href:attr_value(~, 'href'); *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'img') { <img src:attr_value(~, 'src'), alt:attr_value(~, 'alt')> }
  else if (~.kind == 'node' and ~.tag == 'hr') { <hr> }
  else if (~.kind == 'node' and ~.tag == 'table') { <table; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'thead') { <thead; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'tbody') { <tbody; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'tfoot') { <tfoot; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'tr') { <tr; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'th') { <th; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'td') { <td; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node') { <div; *[for (c in ~.content) apply(c)]> }
  else { "" }
}

view <h1> { <h1; apply;> }
view <h2> { <h2; apply;> }
view <h3> { <h3; apply;> }
view <h4> { <h4; apply;> }
view <h5> { <h5; apply;> }
view <h6> { <h6; apply;> }
view <p> { <p; apply;> }
view <span> { <span; apply;> }
view <strong> { <strong; apply;> }
view <em> { <em; apply;> }
view <u> { <u; apply;> }
view <code> { <code; apply;> }
view <a> { <a href:~.href; apply;> }
view <ul> { <ul; apply;> }
view <ol> { <ol; apply;> }
view <li> { <li; apply;> }
view <blockquote> { <blockquote; apply;> }
view <table> { <table; apply;> }
view <thead> { <thead; apply;> }
view <tbody> { <tbody; apply;> }
view <tfoot> { <tfoot; apply;> }
view <tr> { <tr; apply;> }
view <th> { <th; apply;> }
view <td> { <td; apply;> }
view <hr> { <hr> }
view <img> { <img src:~.src, alt:~.alt> }
view <body> { <div class:"doc-body"; apply;> }

// ============================================================================
// Toolbar — every button is one `view <toolbar_button>` instance whose
// click handler emits an `rte_cmd` event up to the surrounding `edit <rte_app>`.
// ============================================================================

view <toolbar_button> {
  <button class:("toolbar-btn " ++ ~.cmd); ~.label>
}
on click() {
  emit("rte_cmd", ~.cmd)
}

// ============================================================================
// Top-level reactive editor application
// ============================================================================

edit <rte_app> state editor: initial_editor, status: ("opened:" ++ SOURCE_PATH), markdown_output: "", drag_selection: null, drag_moved: false {
  <div class:"rte-app"
    <div id:"toolbar", class:"toolbar"
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
    <div id:"doc", contenteditable:"true", tabindex:"0", class:"doc-host"
      apply(editor.doc)
    >
    <pre id:"markdown-output"; markdown_output>
    <div id:"status"; status>
  >
}
on mousemove(evt) {
  let sel = normalize_source_selection(editor.doc, evt.source_selection)
  if (evt.selection_press_in_range and source_selection_is_range(sel)) {
    drag_selection = sel
    drag_moved = true
    status = "dragging"
  } else if (evt.selection_press_in_range and drag_selection != null) {
    drag_moved = true
    status = "dragging"
  }
}
on mouseup(evt) {
  if (drag_selection != null and drag_moved) {
    let drop_pos = normalize_source_pos(editor.doc, evt.source_pos)
    if (drop_pos != null) {
      let moved_text = selection_to_string(editor.doc, drag_selection)
      let delete_editor = edit_set_selection(editor, drag_selection)
      if (edit_can_exec(delete_editor, edit_cmd_delete_forward())) {
        let after_delete = edit_exec(delete_editor, edit_cmd_delete_forward())
        let drop_after_delete = normalize_source_pos(after_delete.doc, drop_pos)
        if (drop_after_delete != null) {
          let insert_editor = edit_set_selection(after_delete, text_selection(drop_after_delete, drop_after_delete))
          if (edit_can_exec(insert_editor, edit_cmd_insert_text(moved_text))) {
            editor = edit_exec(insert_editor, edit_cmd_insert_text(moved_text))
            set_selection(editor.selection)
            status = "drag-moved"
          } else {
            status = "drag-insert-null:" ++ moved_text
          }
        } else {
          status = "drag-drop-null:" ++ moved_text
        }
      } else {
        let move_cmd = edit_cmd_move_text_selection(drag_selection, drop_pos)
        if (edit_can_exec(editor, move_cmd)) {
          editor = edit_exec(editor, move_cmd)
          set_selection(editor.selection)
          status = "drag-moved"
        } else {
          status = "drag-delete-null:" ++ moved_text
        }
      }
    }
  }
  drag_selection = null
  drag_moved = false
}
on beforeinput(evt) {
  if (evt.input_intent != null) {
    editor = edit_dispatch(editor_with_event_selection(editor, evt), evt.input_intent)
    set_selection(editor.selection)
    status = evt.input_type
  } else {
    status = evt.input_type
  }
}
on click(evt) {
  if (evt.target_tag != "button" and (evt.source_selection != null or evt.source_pos != null)) {
    editor = editor_with_click_selection(editor, evt)
    status = "selected"
  }
}
on selectionchange(evt) {
  if (evt.source_selection != null or evt.source_pos != null) {
    editor = editor_with_event_selection(editor, evt)
    set_selection(editor.selection)
    status = "selected"
  }
}
on rte_cmd(evt) {
  // S2.1 dispatch stub — proves toolbar -> state path. S2.2 replaces these
  // branches with `editor = edit_exec(editor, edit_cmd_*())` from the
  // imported `mod_editor` module.
  let cmd = evt
  if (cmd == "btn-save") {
    markdown_output = doc_text(editor.doc)
    status = "saved"
  } else if (cmd == "btn-bold") {
    editor = edit_exec(editor, edit_cmd_toggle_mark('strong'))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-italic") {
    editor = edit_exec(editor, edit_cmd_toggle_mark('em'))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-underline") {
    editor = edit_exec(editor, edit_cmd_toggle_mark('u'))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-ul") {
    editor = edit_exec(editor, edit_cmd_wrap_list('bullet'))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-ol") {
    editor = edit_exec(editor, edit_cmd_wrap_list('ordered'))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-quote") {
    editor = edit_exec(editor, edit_cmd_wrap_blockquote())
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-code") {
    editor = edit_exec(editor, edit_cmd_insert_code_block(""))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-link") {
    editor = edit_exec(editor, edit_cmd_insert_link("https://example.com", "", "Example"))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-image") {
    editor = edit_exec(editor, edit_cmd_insert_image("https://example.com/image.png", "Example image"))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-table") {
    editor = edit_exec(editor, edit_cmd_insert_table(2, 2, true))
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-undo") {
    editor = edit_exec(editor, edit_cmd_history_undo())
    set_selection(editor.selection)
    status = cmd
  } else if (cmd == "btn-redo") {
    editor = edit_exec(editor, edit_cmd_history_redo())
    set_selection(editor.selection)
    status = cmd
  } else {
    status = cmd
  }
}

// ============================================================================
// Page shell
// ============================================================================

<html lang:"en"
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
