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
  {cmd: "link", label: "Link", title: "Insert link", dialog: 'link'},
  {cmd: "image", label: "Image", title: "Insert image", dialog: 'image'},
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

fn item_disabled(item, editor, dirty, ds) {
  if (item.cmd == "save") not dirty
  else if (item.needs_pick == true) ds == null or len(ds.picked) == 0
  else if (item.history == 'undo') not edit_can_undo(editor)
  else if (item.history == 'redo') not edit_can_redo(editor)
  else false
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
          ["aria-disabled"]: if (~.disabled) "true" else "false", ~.label>
}
on click(evt) {
  if (not ~.disabled) { emit("edit_cmd", {cmd: ~.cmd, node: evt.target}) }
}

pub let css = "
  .edit-toolbar { position: sticky; top: 0; z-index: 10; display: flex; flex-wrap: wrap;
                  align-items: center; gap: 6px; padding: 8px 12px; background: #f6f8fa;
                  border-bottom: 1px solid #d0d7de; }
  .edit-file { display: flex; align-items: center; gap: 6px; margin-right: 8px;
               font-weight: 600; color: #1f2328; font-size: 14px; }
  .edit-dirty { color: #bf8700; }
  .edit-group { display: flex; gap: 2px; padding-right: 8px; margin-right: 2px;
                border-right: 1px solid #d0d7de; }
  .edit-group:last-child { border-right: none; }
  .edit-btn { min-width: 30px; height: 28px; padding: 0 8px; border: 1px solid transparent;
              border-radius: 6px; background: transparent; color: #1f2328; font-size: 13px;
              cursor: pointer; white-space: nowrap; }
  .edit-btn:hover { background: #eaeef2; }
  .edit-btn.active { background: #ddf4ff; border-color: #54aeff; }
  .edit-btn.disabled { color: #8c959f; cursor: default; }
  .edit-btn-bold { font-weight: 700; }
  .edit-btn-italic { font-style: italic; }
  .edit-btn-underline { text-decoration: underline; }
  .edit-btn-strike { text-decoration: line-through; }
"
