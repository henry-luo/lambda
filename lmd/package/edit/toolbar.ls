// toolbar.ls — the edit application's top toolbar
// (vibe/radiant/Radiant_Design_Edit_Mode.md §7).
//
// Each button is a command descriptor: an application command (save), a
// dialog opener, or an editing request by its WHATWG input type. Buttons do
// not edit anything themselves; the shell resolves the command through the
// same descriptor/request path as keyboard input (Lambda DOM Editable §8.3).
// Active and disabled state come from model queries, never a UI switch.

import lambda.editor.mod_editor

// Shared by every format: file and history commands.
pub let file_group = [
  {cmd: "save", label: "Save", title: "Save (Cmd/Ctrl+S)", app: true},
  {cmd: "save_as", label: "Save As", title: "Save As (Shift+Cmd/Ctrl+S)", app: true},
  {cmd: "undo", label: "Undo", title: "Undo (Cmd/Ctrl+Z)", input_type: "historyUndo", payload: {}, history: 'undo'},
  {cmd: "redo", label: "Redo", title: "Redo (Shift+Cmd/Ctrl+Z)", input_type: "historyRedo", payload: {}, history: 'redo'}
]

// The view switch (Radiant_Design_Source_Editor OQ7): each surface offers
// the other view of the same file.
pub let source_view_group = [{cmd: "view_source", label: "Source", title: "Show the source (Cmd/Ctrl+/)", app: true}]
pub let rich_view_group = [{cmd: "view_rich", label: "Rich", title: "Show the rich view (Cmd/Ctrl+/)", app: true}]

fn block_button(tag, label, title) =>
  {cmd: string(tag), label: label, title: title, input_type: "formatBlock", payload: {tag: tag}, block: tag}

fn mark_button(cmd, label, title, input_type, mark) =>
  {cmd: cmd, label: label, title: title, input_type: input_type,
   payload: {mark: mark, value: true}, mark: mark}

let block_group = [
  block_button('p', "P", "Paragraph"),
  block_button('h1', "H1", "Heading 1"),
  block_button('h2', "H2", "Heading 2"),
  block_button('h3', "H3", "Heading 3")
]

let insert_group = [
  {cmd: "link", label: "Link", title: "Insert link", dialog: 'link', input_type: "insertLink"},
  {cmd: "image", label: "Image", title: "Insert image", dialog: 'image', input_type: "insertImage"},
  {cmd: "codeblock", label: "Code block", title: "Code block", input_type: "insertCodeBlock", payload: {data: ""}, block: 'pre'},
  {cmd: "rule", label: "Rule", title: "Horizontal rule", input_type: "insertHorizontalRule", payload: {}}
]

let structure_group = [
  {cmd: "ul", label: "List", title: "Bulleted list", input_type: "insertUnorderedList", payload: {kind: 'bullet'}, inside: 'ul'},
  {cmd: "ol", label: "1. List", title: "Numbered list", input_type: "insertOrderedList", payload: {kind: 'ordered'}, inside: 'ol'},
  {cmd: "indent", label: "Indent", title: "Indent list item (Tab)", input_type: "formatIndent", payload: {}},
  {cmd: "outdent", label: "Outdent", title: "Outdent list item (Shift+Tab)", input_type: "formatOutdent", payload: {}},
  {cmd: "quote", label: "Quote", title: "Block quote", input_type: "formatBlockquote", payload: {},
   inside: 'blockquote', lift: "formatLiftBlockquote"}
]

fn mark_group(underline) {
  let base = [
    mark_button("bold", "B", "Bold (Cmd/Ctrl+B)", "formatBold", 'strong'),
    mark_button("italic", "I", "Italic (Cmd/Ctrl+I)", "formatItalic", 'em')
  ]
  let under = if (underline) [mark_button("underline", "U", "Underline (Cmd/Ctrl+U)", "formatUnderline", 'u')] else []
  let rest = [
    mark_button("strike", "S", "Strikethrough", "modelToggleMark", 'del'),
    mark_button("code", "Code", "Inline code", "modelToggleMark", 'code')
  ];
  [*base, *under, *rest]
}

// Rich-text command groups for a profile; underline only where the format
// can spell it (Markdown cannot).
pub fn rich_text_groups(underline) => [block_group, mark_group(underline), structure_group, insert_group]

// Drawing groups (proposal §7): tools, object edits, and the view. A tool
// item is active while its tool is; an object edit needs a selection.
fn tool_button(tool, label, title) => {cmd: "tool_" ++ string(tool), label: label, title: title, tool: tool}

fn object_button(cmd, label, title) => {cmd: cmd, label: label, title: title, needs_pick: true}

let drawing_tool_group = [
  tool_button('select', "Select", "Select and move (V)"),
  tool_button('rect', "Rect", "Rectangle (R)"),
  tool_button('ellipse', "Ellipse", "Ellipse (E)"),
  tool_button('line', "Line", "Line (L)"),
  tool_button('text', "Text", "Text (T)")
]

let drawing_object_group = [
  // with nothing picked, Style sets the paint of new shapes
  {cmd: "style", label: "Style", title: "Fill and stroke"},
  object_button("duplicate", "Duplicate", "Duplicate (Cmd/Ctrl+D)"),
  object_button("delete", "Delete", "Delete (Delete)"),
  object_button("front", "Front", "Bring to front"),
  object_button("back", "Back", "Send to back")
]

let drawing_view_group = [
  {cmd: "zoom_out", label: "-", title: "Zoom out (Cmd/Ctrl+-)"},
  {cmd: "zoom_fit", label: "Fit", title: "Zoom to fit"},
  {cmd: "zoom_in", label: "+", title: "Zoom in (Cmd/Ctrl+=)"}
]

pub fn drawing_groups() => [drawing_tool_group, drawing_object_group, drawing_view_group]

// The item with `cmd`, searched across groups, or null.
pub fn find_item(groups, cmd) {
  let found = [for (g in groups) for (item in g where item.cmd == cmd) item]
  if (len(found) == 0) null else found[0]
}

// `ds` is the drawing session state, null on a rich-text surface.
fn item_active(item, editor, ds) {
  if (item.tool != null) ds != null and ds.tool == item.tool
  else if (item.mark != null) edit_mark_active(editor, item.mark)
  else if (item.block != null) edit_enclosing_tag(editor, ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'pre']) == item.block
  else if (item.inside != null) edit_inside(editor, item.inside)
  else false
}

// The source surface passes {kind: 'source', can_undo, can_redo} for
// `editor`: its buffer history is not a lambda.editor history (source.ls).
fn can_undo(editor) => if (editor.kind == 'source') editor.can_undo else edit_can_undo(editor)
fn can_redo(editor) => if (editor.kind == 'source') editor.can_redo else edit_can_redo(editor)

fn item_disabled(item, editor, dirty, ds) {
  if (item.cmd == "save") not dirty
  else if (item.needs_pick == true) ds == null or len(ds.picked) == 0
  else if (item.history == 'undo') not can_undo(editor)
  else if (item.history == 'redo') not can_redo(editor)
  else false
}

// A single stroke vocabulary keeps icons aligned at every toolbar size.
let icon_paths = {
  save: "M5 3h12l4 4v14H3V3h2 M7 3v6h10V3 M7 21v-8h10v8",
  undo: "M9 4 4 9l5 5 M4 9h10a6 6 0 0 1 0 12",
  redo: "m15 4 5 5-5 5 M20 9H10a6 6 0 0 0 0 12",
  code: "m8 6-6 6 6 6 m8-12 6 6-6 6 m-3-14-2 16",
  ul: "M9 6h12 M9 12h12 M9 18h12 M3 6h1 M3 12h1 M3 18h1",
  ol: "M10 6h11 M10 12h11 M10 18h11 M3 3h1v6 M3 9h3 M3 14c4-2 4 2 0 5h3",
  indent: "M3 4h18 M11 9h10 M11 15h10 M3 20h18 m0-12 4 4-4 4",
  outdent: "M3 4h18 M11 9h10 M11 15h10 M3 20h18 m4-12-4 4 4 4",
  quote: "M4 6h6v7H4V6 M10 13c0 4-2 5-5 5 M14 6h6v7h-6V6 M20 13c0 4-2 5-5 5",
  link: "m10 13 4-4 M8 16l-1 1a4 4 0 0 1-6-6l4-4a4 4 0 0 1 6 0 M16 8l1-1a4 4 0 0 1 6 6l-4 4a4 4 0 0 1-6 0",
  image: "M3 3h18v18H3V3 M3 17l6-6 4 4 3-3 5 5 M8 7h.01",
  codeblock: "M3 3h18v18H3V3 m5 5-3 4 3 4 m8-8 3 4-3 4",
  rule: "M3 12h18",
  view_source: "m8 6-6 6 6 6 m8-12 6 6-6 6",
  view_rich: "M4 5h16 M4 10h16 M4 15h10 M4 20h13"
}

fn icon(cmd) =>
  <svg class: "edit-icon", width: "18", height: "18", viewBox: "0 0 24 24", fill: "none",
       stroke: "currentColor", ["stroke-width"]: "1.65", ["stroke-linecap"]: "round",
       ["stroke-linejoin"]: "round", ["aria-hidden"]: "true",
    <path d: icon_paths[cmd]>
  >

fn button_content(b) {
  let has_icon = icon_paths[b.cmd] != null
  let with_label = member_label(b.cmd);
  [if (has_icon) icon(b.cmd) else null,
   if (not has_icon or with_label) <span class: "edit-btn-label", b.label> else null]
}

fn member_label(cmd) => cmd == "save" or cmd == "view_source" or cmd == "view_rich"

// File actions have their own row; formatting groups wrap as units and never
// change height when the filename gains its dirty indicator.
pub fn toolbar(file, editor, dirty, ds, groups, view_items) {
  let history = group(drop(file_group, 2), editor, dirty, ds)
  let has_formatting = len(groups) > 0;
  <div class: "edit-toolbar",
    <div class: "edit-document-bar",
      file;
      <div class: "edit-document-actions",
        if (not has_formatting) history else null;
        group(view_items, editor, dirty, ds)
        group(take(file_group, 2), editor, dirty, ds)
      >
    >
    if (has_formatting) <div class: "edit-format-bar", role: "toolbar", ["aria-label"]: "Document formatting",
      history;
      *[for (items in groups) group(items, editor, dirty, ds)]
      if (ds != null) <span class: "edit-zoom", string(round(ds.zoom * 100.0)) ++ "%"> else null
    > else null
  >
}

// A toolbar button bound to its computed state.
fn button(item, editor, dirty, ds) =>
  apply(<edit_button cmd: item.cmd, label: item.label, title: item.title,
                     active: item_active(item, editor, ds), disabled: item_disabled(item, editor, dirty, ds)>)

pub fn group(items, editor, dirty, ds) =>
  <div class: "edit-group", role: "group", *[for (item in items) button(item, editor, dirty, ds)]>

fn button_class(b) =>
  "edit-btn edit-btn-" ++ b.cmd ++ (if (b.active) " active" else "") ++ (if (b.disabled) " disabled" else "")

// Focus stays in the document while a button is pressed; the click hands the
// command and its node (for window-level requests) up to the shell.
view <edit_button> {
  <button class: button_class(~), title: ~.title,
          ["aria-pressed"]: if (~.active) "true" else "false",
          ["aria-disabled"]: if (~.disabled) "true" else "false",
          ["aria-label"]: ~.title, *button_content(~)>
}
on mousedown(evt) {
  'prevent-default'
}
on click(evt) {
  if (not ~.disabled) { emit("edit_cmd", {cmd: ~.cmd, node: evt.target}) }
}

pub let css = "
  .edit-toolbar { position: sticky; top: 0; z-index: 10; flex: none; background: #ffffff;
                  border-bottom: 1px solid #dce2ea; box-shadow: 0 2px 6px rgba(20, 30, 50, 0.04); }
  .edit-document-bar { display: flex; align-items: center; justify-content: space-between;
                        gap: 16px; padding: 10px 20px; border-bottom: 1px solid #edf0f4; }
  .edit-format-source .edit-document-bar { padding: 6px 20px; border-bottom: none; }
  .edit-document-actions { display: flex; align-items: center; flex: none; gap: 12px; }
  .edit-file { display: flex; align-items: center; gap: 6px; min-width: 0;
               font-weight: 600; color: #253047; font-size: 13px; }
  .edit-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .edit-dirty { flex: none; width: 8px; color: #b17a24; }
  .edit-format-bar { display: flex; flex-wrap: wrap; align-items: center; gap: 8px;
                      padding: 7px 20px; background: #fafbfc; }
  .edit-group { display: flex; align-items: center; flex: none; gap: 3px;
                padding-right: 10px; border-right: 1px solid #e2e7ef; }
  .edit-group:last-child { padding-right: 0; border-right: none; }
  .edit-btn { display: inline-flex; align-items: center; justify-content: center; gap: 6px;
              box-sizing: border-box; min-width: 32px; height: 32px; padding: 0 8px;
              border: 1px solid transparent; border-radius: 6px; background: transparent;
              color: #475569; font-family: inherit; font-size: 13px; line-height: 1;
              cursor: pointer; white-space: nowrap; }
  .edit-btn:hover { background: #edf1f7; color: #1e293b; }
  .edit-btn.active { background: #e8effb; border-color: #c9d9f3; color: #285bb0; }
  .edit-btn:focus-visible { outline: 2px solid #799ed4; outline-offset: 2px; }
  .edit-btn.disabled { color: #a6afbc; cursor: default; }
  .edit-btn.disabled:hover { background: transparent; }
  .edit-icon { display: block; flex: none; pointer-events: none; }
  .edit-btn-save { background: #315eab; border-color: #315eab; color: #ffffff; padding: 0 12px; }
  .edit-btn-save:hover { background: #264e93; color: #ffffff; }
  .edit-btn-save.disabled { background: #eef2f7; border-color: #e2e7ef; color: #8c99ac; }
  .edit-btn-save_as { border-color: #e2e7ef; background: #ffffff; }
  .edit-btn-view_source, .edit-btn-view_rich { font-size: 12px; }
  .edit-btn-bold { font-weight: 700; font-size: 15px; }
  .edit-btn-italic { font-style: italic; font-family: Georgia, serif; font-size: 16px; }
  .edit-btn-underline { text-decoration: underline; }
  .edit-btn-strike { text-decoration: line-through; }
  .edit-btn-h1, .edit-btn-h2, .edit-btn-h3 { font-size: 12px; font-weight: 600; }
"
