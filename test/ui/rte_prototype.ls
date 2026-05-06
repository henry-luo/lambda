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
import lambda.package.editor.mod_source_pos

let SOURCE_PATH = './test/input/simple.md'
let initial_doc^err = input(SOURCE_PATH, 'markdown')
let initial_body = initial_doc[0]

fn mark_to_editor(item) {
  if (type(item) == string) { text(item) }
  else if (type(item) == element) { node(name(item), [for (i in 0 to len(item) - 1) mark_to_editor(item[i])]) }
  else { text(string(item)) }
}

let initial_editor_doc = node('doc', [for (child in initial_body) mark_to_editor(child)])
let initial_title_len = len(doc_text(node_at(initial_editor_doc, [0])))
let initial_selection = text_selection(pos([0, 0], initial_title_len), pos([0, 0], initial_title_len))
let initial_editor = edit_open(initial_editor_doc, null, initial_selection)

fn event_selection(evt, fallback) {
  if (evt.source_selection != null) { evt.source_selection }
  else if (evt.source_pos != null) { text_selection(evt.source_pos, evt.source_pos) }
  else { fallback }
}

fn editor_with_event_selection(ed, evt) => edit_set_selection(ed, event_selection(evt, ed.selection))

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
  if (~.kind == 'text') { ~.text }
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
  else if (~.kind == 'node' and (~.tag == 'code' or ~.tag == 'code_block')) { <code; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and (~.tag == 'ul' or ~.tag == 'list')) { <ul; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'ol') { <ol; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'li') { <li; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'blockquote') { <blockquote; *[for (c in ~.content) apply(c)]> }
  else if (~.kind == 'node' and ~.tag == 'hr') { <hr> }
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

edit <rte_app> state editor: initial_editor, status: ("opened:" ++ SOURCE_PATH), markdown_output: "" {
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
on beforeinput(evt) {
  if (evt.input_intent != null) {
    editor = edit_dispatch(editor, evt.input_intent)
    status = evt.input_type
  } else {
    status = evt.input_type
  }
}
on click(evt) {
  if (evt.source_selection != null or evt.source_pos != null) {
    editor = editor_with_event_selection(editor, evt)
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
    status = cmd
  } else if (cmd == "btn-italic") {
    editor = edit_exec(editor, edit_cmd_toggle_mark('em'))
    status = cmd
  } else if (cmd == "btn-underline") {
    editor = edit_exec(editor, edit_cmd_toggle_mark('u'))
    status = cmd
  } else if (cmd == "btn-undo") {
    editor = edit_exec(editor, edit_cmd_history_undo())
    status = cmd
  } else if (cmd == "btn-redo") {
    editor = edit_exec(editor, edit_cmd_history_redo())
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
      .toolbar { display: flex; gap: 4px; padding: 10px 14px;
                 background: #f8f9fb; border-bottom: 1px solid #e5e7eb; }
      .toolbar-btn { min-width: 32px; height: 32px; padding: 0 8px;
                     border: 1px solid #d8dadf; background: white;
                     border-radius: 6px; cursor: pointer; font-size: 14px; }
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
      .doc-host a { color: #1d4ed8; text-decoration: underline; }
      #markdown-output { display: none; }
      #status { padding: 8px 14px; background: #f1f3f5; color: #555;
                font-size: 12px; border-top: 1px solid #e5e7eb; font-family: monospace; }
    ">
  >
  <body
    apply(<rte_app>, {mode: "edit"})
  >
>
