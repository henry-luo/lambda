import { DOMParser } from "prosemirror-model";
import { schema } from "prosemirror-schema-basic";
import { EditorState } from "prosemirror-state";
import { EditorView } from "prosemirror-view";
import { baseKeymap, lift, setBlockType, toggleMark, wrapIn } from "prosemirror-commands";
import { history, undo, redo } from "prosemirror-history";
import { keymap } from "prosemirror-keymap";

const host = document.getElementById("editor");
const state = document.getElementById("state");
const toolbar = document.getElementById("toolbar");
const editorSchema = schema;
const read_only = host.hasAttribute("data-readonly");
const initial_doc = DOMParser.fromSchema(editorSchema).parse(host);
let view = null;

function publish() {
  state.textContent = JSON.stringify({
    doc: view.state.doc.toJSON(),
    text: view.state.doc.textContent,
    selection: { from: view.state.selection.from, to: view.state.selection.to }
  });
  sync_toolbar();
}

function selection_has_node_type(selection, node_type) {
  for (const position of [selection.$from, selection.$to]) {
    for (let depth = position.depth; depth > 0; depth--) {
      if (position.node(depth).type == node_type) return true;
    }
  }
  return false;
}

function mark_is_active(mark_type) {
  const selection = view.state.selection;
  if (selection.empty) return mark_type.isInSet(selection.$from.marks()) != null;
  return view.state.doc.rangeHasMark(selection.from, selection.to, mark_type);
}

function run_toolbar_command(command) {
  if (!view || !command(view.state, view.dispatch, view)) return;
  view.focus();
}

function toggle_blockquote(state, dispatch) {
  if (selection_has_node_type(state.selection, editorSchema.nodes.blockquote)) {
    return lift(state, dispatch);
  }
  return wrapIn(editorSchema.nodes.blockquote)(state, dispatch);
}

const toolbar_commands = {
  paragraph: setBlockType(editorSchema.nodes.paragraph),
  heading: setBlockType(editorSchema.nodes.heading, { level: 2 }),
  bold: toggleMark(editorSchema.marks.strong),
  italic: toggleMark(editorSchema.marks.em),
  code: toggleMark(editorSchema.marks.code),
  quote: toggle_blockquote,
  undo,
  redo
};

function sync_toolbar_button(name, active) {
  if (!toolbar) return;
  const button = toolbar.querySelector(`[data-command="${name}"]`);
  if (!button) return;
  button.setAttribute("aria-pressed", active ? "true" : "false");
}

function sync_toolbar() {
  if (!toolbar || !view) return;
  const selection = view.state.selection;
  sync_toolbar_button("paragraph", selection.$from.parent.type == editorSchema.nodes.paragraph);
  sync_toolbar_button("heading", selection.$from.parent.type == editorSchema.nodes.heading);
  sync_toolbar_button("bold", mark_is_active(editorSchema.marks.strong));
  sync_toolbar_button("italic", mark_is_active(editorSchema.marks.em));
  sync_toolbar_button("code", mark_is_active(editorSchema.marks.code));
  sync_toolbar_button("quote", selection_has_node_type(selection, editorSchema.nodes.blockquote));
  sync_toolbar_button("undo", false);
  sync_toolbar_button("redo", false);
}

function bind_toolbar() {
  if (!toolbar) return;
  toolbar.addEventListener("mousedown", (event) => {
    const button = event.target.closest("button[data-command]");
    // keep the DOM range on the editor so a button command formats its selection.
    if (button) event.preventDefault();
  });
  toolbar.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-command]");
    if (!button) return;
    const command = toolbar_commands[button.getAttribute("data-command")];
    if (command) run_toolbar_command(command);
  });
}

// editorview appends its live root to the mount, so remove the parsed seed DOM
// before mounting to avoid showing a static duplicate beside the editable view.
host.textContent = "";

view = new EditorView(host, {
  state: EditorState.create({
    doc: initial_doc,
    plugins: [
      history(),
      keymap({ "Mod-z": undo, "Mod-y": redo, "Mod-Shift-z": redo }),
      keymap(baseKeymap)
    ]
  }),
  dispatchTransaction(transaction) {
    view.updateState(view.state.apply(transaction));
    publish();
  },
  editable() { return !read_only; }
});
bind_toolbar();
publish();

function destroy_editor() {
  if (!view) return;
  // ProseMirror must release its DOM observers before the fixture removes the
  // live view, otherwise a stale observer can publish after teardown.
  view.destroy();
  view = null;
  state.setAttribute("data-destroyed", "true");
}

document.getElementById("destroy").addEventListener("click", destroy_editor);
window.__editableProbe = {
  read() { return state.textContent; },
  destroy: destroy_editor
};
