import { DOMParser } from "prosemirror-model";
import { schema } from "prosemirror-schema-basic";
import { EditorState } from "prosemirror-state";
import { EditorView } from "prosemirror-view";
import { baseKeymap } from "prosemirror-commands";
import { history, undo, redo } from "prosemirror-history";
import { keymap } from "prosemirror-keymap";

const host = document.getElementById("editor");
const state = document.getElementById("state");
const editorSchema = schema;
const read_only = host.hasAttribute("data-readonly");
let view = null;

function publish() {
  state.textContent = JSON.stringify({
    doc: view.state.doc.toJSON(),
    text: view.state.doc.textContent,
    selection: { from: view.state.selection.from, to: view.state.selection.to }
  });
}

view = new EditorView(host, {
  state: EditorState.create({
    doc: DOMParser.fromSchema(editorSchema).parse(host),
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
