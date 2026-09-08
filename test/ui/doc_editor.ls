// doc_editor.ls — Phase 1 document-editor prototype
//
// A read-only project browser built with Lambda's reactive UI.  The left
// panel is rooted at the current project directory; directories are loaded
// only when opened so the prototype remains useful for large worktrees.
//
// Run:
//   ./lambda.exe view test/ui/doc_editor.ls
// Headless smoke:
//   ./lambda.exe view test/ui/doc_editor.ls --headless --no-log

import latex: lambda.latex.latex

let PROJECT_ROOT = "."

// --------------------------------------------------------------------------
// Filesystem and selection helpers
// --------------------------------------------------------------------------

fn directory_entries(path) => input(path, 'dir') ^ { [] }
fn child_path(parent_path, child_name) => join([parent_path, child_name], "/")

fn path_is_open(open_paths, path) => contains(open_paths, path)
fn remove_path(paths, path) => [for (candidate in paths where candidate != path) candidate]
fn tree_hit_class(path) => "tree-hit-" ++ replace(replace(path, "/", "_"), ".", "_")
fn event_hits_tree_row(evt, hit_class) => contains(evt["target_parent_class"], hit_class ++ " ")

fn bounded_text_offset(value, offset) {
  if (offset < 0) { 0 }
  else if (offset > len(value)) { len(value) }
  else { offset }
}

fn text_before_input(value, evt) {
  let selection_start = evt["selection_start"]
  let caret_pos = evt["caret_pos"]
  if (selection_start != null) { bounded_text_offset(value, selection_start) }
  else if (caret_pos != null) { bounded_text_offset(value, caret_pos) }
  else { len(value) }
}

fn text_after_input(value, evt) {
  let selection_end = evt["selection_end"]
  if (selection_end != null) { bounded_text_offset(value, selection_end) }
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
  // Hide implementation artefacts without preventing normal source browsing.
  not starts_with(entry_name, ".") and
    (not entry["is_dir"] or
      (not starts_with(entry_name, "build") and not starts_with(entry_name, "release")))
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
  else if (contains(["htm", "html"], ext)) { "html" }
  else { null }
}

fn is_latex_document(extension) {
  let ext = lower(extension)
  ext == "tex" or ext == "latex"
}

fn is_renderable_document(extension) =>
  document_format(extension) != null or is_latex_document(extension)

fn selected_source(file) {
  if (file == null) { "" }
  else {
    let selected_path = file["file_path"];
    input(selected_path, 'text') ^ { "Unable to read selected file" }
  }
}

fn selected_preview(file) {
  if (file == null) { null }
  else {
    let selected_path = file["file_path"]
    if (is_latex_document(file["extension"])) {
      latex.render_file(selected_path) ^ { <p class:"preview-error", "Unable to render selected file"> }
    }
    else {
      let format = document_format(file["extension"])
      if (format == null) { null }
      else { input(selected_path, format) ^ { <p class:"preview-error", "Unable to render selected file"> } }
    }
  }
}

// --------------------------------------------------------------------------
// Mark document preview templates
//
// Markdown, wiki, RST, and HTML readers produce the same HTML-shaped Mark
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
view <h1> { <h1 *[rendered_children(~)]> }
view <h2> { <h2 *[rendered_children(~)]> }
view <h3> { <h3 *[rendered_children(~)]> }
view <h4> { <h4 *[rendered_children(~)]> }
view <h5> { <h5 *[rendered_children(~)]> }
view <h6> { <h6 *[rendered_children(~)]> }
view <p> { <p *[rendered_children(~)]> }
view <span> { <span *[rendered_children(~)]> }
view <strong> { <strong *[rendered_children(~)]> }
view <em> { <em *[rendered_children(~)]> }
view <del> { <del *[rendered_children(~)]> }
view <u> { <u *[rendered_children(~)]> }
view <code> {
  if (~.type == "block") { <pre <code *[rendered_children(~)]>> }
  else { <code *[rendered_children(~)]> }
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
view <thead> { <thead *[rendered_children(~)]> }
view <tbody> { <tbody *[rendered_children(~)]> }
view <tfoot> { <tfoot *[rendered_children(~)]> }
view <tr> { <tr *[rendered_children(~)]> }
view <th> { <th *[rendered_children(~)]> }
view <td> { <td *[rendered_children(~)]> }

// --------------------------------------------------------------------------
// Lazy file-tree rows
// --------------------------------------------------------------------------

view <tree_entry> {
  let entry_path = child_path(~.parent_path, ~.name)
  let is_open = path_is_open(~.open_paths, entry_path)
  let indent = (~.depth * 16) ++ "px"
  let hit_class = tree_hit_class(entry_path)
  let row_class = if (~.selected_path == entry_path) {
    hit_class ++ " tree-row selected"
  } else {
    hit_class ++ " tree-row"
  }
  let children = if (~.is_dir and is_open) { directory_entries(entry_path) } else { [] };

  <div class:"tree-entry"
  , <div class:row_class, style:("padding-left:" ++ indent)
    , if (~.is_dir) {
        <button class:"tree-toggle",
          if (is_open) "▾" else "▸">
      } else {
        <span class:"tree-spacer", "">
      }
      <span class:(if (~.is_dir) "tree-icon folder-icon" else "tree-icon file-icon"),
        if (~.is_dir) "▣" else "▤">
      <span class:"tree-label", ~.name>
    >
    if (~.is_dir and is_open) {
      <div class:"tree-children"
      , for (child in children where entry_matches_filter(child, ~.filter_text))
          apply(<tree_entry
            // Recompute per child: retaining entry_path here appends a prior
            // sibling during reactive list reconciliation.
            parent_path:child_path(~.parent_path, ~.name),
            name:child.name,
            extension:child.extension,
            is_dir:child.is_dir,
            depth:(~.depth + 1),
            open_paths:~.open_paths,
            selected_path:~.selected_path,
            filter_text:~.filter_text
          >)
      >
    }
  >
}
on click(evt) {
  let target_class = evt["target_class"]
  let entry_path = child_path(~.parent_path, ~.name)
  let hit_class = tree_hit_class(entry_path)
  let hits_this_row = event_hits_tree_row(evt, hit_class)
  if (~.is_dir and hits_this_row and
      (target_class == "tree-toggle" or target_class == "tree-label" or target_class == "folder-icon")) {
    emit("tree_toggle", {file_path:entry_path})
  } else if (not ~.is_dir) {
    emit("file_select", {file_path:entry_path, name:~.name, extension:~.extension})
  }
}

view <document_pane> {
  if (~.file == null) {
    <main class:"document-panel"
    , <div id:"empty-preview", class:"empty-preview"
      , <div class:"empty-preview-icon", "▤">
        <h1 "Open a file">
        <p "Choose a file from the project tree to inspect it.">
        <p class:"empty-preview-note", "Markdown, wiki, RST, HTML, and LaTeX files open as rendered documents; all other files open as source.">
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
      if (~.preview_mode == "view") {
        let preview = selected_preview(~.file);
        <section id:"rendered-preview", class:"rendered-preview", apply(preview)>
      } else {
        let source = selected_source(~.file);
        <section class:"source-tab-panel"
        , <pre id:"source-preview", class:"source-preview", source>
        >
      }
      <nav class:"document-tabs"
      , <button class:(if (~.preview_mode == "view") "document-tab tab-view active" else "document-tab tab-view"), "View">
        <button class:(if (~.preview_mode == "source") "document-tab tab-source active" else "document-tab tab-source"), "Source">
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

edit <doc_editor_app> state root_open: true, open_paths: [], filter_text: "", selected_file: null, preview_mode: "view" {
  let root_entries = directory_entries(PROJECT_ROOT);

  <div class:"doc-editor"
  , <aside class:"file-panel"
    , <div class:"file-panel-header"
      , <div class:"project-title"
        , <span class:"project-icon", "⌘">
          <span "lambda">
        >
        <input id:"file-filter", type:"search", class:"tree-filter", value:filter_text,
          placeholder:"Filter file names">
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
          , for (entry in root_entries where entry_matches_filter(entry, filter_text))
              apply(<tree_entry
                parent_path:PROJECT_ROOT,
                name:entry.name,
                extension:entry.extension,
                is_dir:entry.is_dir,
                depth:1,
                open_paths:open_paths,
                selected_path:(if (selected_file == null) "" else selected_file["file_path"]),
                filter_text:filter_text
              >)
          >
        }
      >
    <div class:"file-panel-footer", if (filter_text == "") "Project root: ." else "Filtering file names">
  >
  apply(<document_pane file:selected_file, preview_mode:preview_mode>)
  >
}
on click(evt) {
  if (evt["target_class"] == "root-toggle") { root_open = not root_open }
}
on input(evt) {
  let target_class = evt.target_class
  let character = evt.char
  if (target_class == "tree-filter" and character != null and character != "") {
    let caret_pos = evt.caret_pos
    filter_text = slice(filter_text, 0, caret_pos) ++ character ++ slice(filter_text, caret_pos, len(filter_text))
  }
}
on keydown(evt) {
  let target_class = evt.target_class
  let key = evt.key
  if (target_class == "tree-filter") {
    if (key == "Backspace") { filter_text = erase_backwards(filter_text, evt) }
    else if (key == "Delete") { filter_text = erase_forwards(filter_text, evt) }
    else if (key == "Escape") { filter_text = "" }
  }
}
on tree_toggle(entry) {
  let entry_path = entry["file_path"]
  if (path_is_open(open_paths, entry_path)) {
    open_paths = remove_path(open_paths, entry_path)
  } else {
    open_paths = [*open_paths, entry_path]
  }
}
on file_select(entry) {
  selected_file = {file_path: entry["file_path"], name: entry["name"], extension: entry["extension"]}
  preview_mode = "view"
}
on preview_tab(tab) {
  preview_mode = tab
}

// --------------------------------------------------------------------------
// Page shell
// --------------------------------------------------------------------------

<html lang:"en",
  <head
    <meta charset:"UTF-8">
    <title "Lambda Document Editor — Prototype">
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
      .tree-filter { width: 100%; height: 34px; border: 1px solid #465166; border-radius: 6px;
                     background: #292f3d; color: #f5f7fb; padding: 0 10px; outline: none; }
      .tree-filter:focus { border-color: #79a6ff; box-shadow: 0 0 0 2px rgba(121,166,255,.2); }
      .tree-filter::placeholder { color: #9aa5b7; }
      .project-tree { min-height: 0; flex: 1; overflow: auto; padding: 8px 6px 16px; }
      .tree-entry, .tree-children { min-width: max-content; }
      .tree-row { min-height: 28px; display: flex; align-items: center; padding-right: 8px;
                  border-radius: 5px; cursor: pointer; color: #c6cedb; user-select: none; }
      .tree-row:hover { background: #2c3444; color: #fff; }
      .tree-row.selected { background: #365383; color: #fff; }
      .project-root { padding-left: 2px; color: #f5f7fb; font-weight: 650; }
      .tree-toggle, .root-toggle { width: 22px; height: 24px; padding: 0; border: 0; background: transparent;
                                   color: #aeb9ca; cursor: pointer; font-size: 14px; }
      .tree-toggle:hover, .root-toggle:hover { color: #fff; }
      .tree-spacer { width: 22px; height: 24px; }
      .tree-icon { width: 18px; margin-right: 4px; text-align: center; font-size: 13px; }
      .folder-icon { color: #e5bb62; }
      .file-icon { color: #9bbdfc; }
      .tree-label { white-space: nowrap; font-size: 13px; }
      .file-panel-footer { padding: 10px 14px; border-top: 1px solid #343c4d; color: #99a5b7;
                           font-size: 11px; }

      .document-panel { min-width: 0; min-height: 0; flex: 1; display: flex; flex-direction: column; background: #fff; }
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
                        color: #293545; font: 13px/1.55 'SF Mono', Menlo, Consolas, monospace; white-space: pre-wrap; }
      .rendered-preview { min-height: 0; flex: 1; overflow: auto; padding: 30px clamp(24px, 6vw, 80px) 60px; }
      .document-tabs { flex: 0 0 auto; display: flex; gap: 2px; justify-content: flex-end;
                       padding: 7px 18px; border-top: 1px solid #e2e6ec; background: #fbfcfe; }
      .document-tab { padding: 5px 11px; border: 0; border-radius: 5px; background: transparent;
                      color: #68778a; cursor: pointer; font-size: 12px; font-weight: 650; }
      .document-tab:hover { background: #e8edf5; color: #31425a; }
      .document-tab.active { background: #dce9fd; color: #1c5da6; }
      .document-body, .latex-output { max-width: 900px; margin: 0 auto; color: #232a35; font-size: 16px; line-height: 1.65; }
      .document-body h1, .document-body h2, .document-body h3, .document-body h4,
      .latex-output h1, .latex-output h2, .latex-output h3, .latex-output h4 { color: #192130; line-height: 1.25; }
      .document-body h1, .latex-output h1 { margin: .2em 0 .65em; font-size: 2em; }
      .document-body h2, .latex-output h2 { margin: 1.45em 0 .55em; font-size: 1.55em; }
      .document-body h3, .latex-output h3 { margin: 1.3em 0 .45em; font-size: 1.25em; }
      .document-body p, .latex-output p { margin: .75em 0; }
      .document-body ul, .document-body ol, .latex-output ul, .latex-output ol { padding-left: 1.5em; }
      .document-body blockquote, .latex-output blockquote { margin: 1em 0; padding: .15em 1em;
                     border-left: 4px solid #a6badc; color: #4c5c70; background: #f6f9fd; }
      .document-body code, .latex-output code { padding: 2px 5px; border-radius: 4px; background: #eef1f5;
                     font: .9em 'SF Mono', Menlo, monospace; }
      .document-body pre, .latex-output pre { overflow: auto; padding: 13px 15px; border-radius: 6px;
                     background: #f1f3f6; color: #243042; white-space: pre-wrap; }
      .document-body pre code, .latex-output pre code { padding: 0; background: transparent; color: inherit; }
      .document-body a, .latex-output a { color: #1769c2; }
      .document-body table, .latex-output table { width: 100%; border-collapse: collapse; margin: 1em 0; }
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
