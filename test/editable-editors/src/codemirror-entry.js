import { EditorState } from "@codemirror/state";
import { EditorView, keymap } from "@codemirror/view";
import { defaultKeymap, history, historyKeymap } from "@codemirror/commands";

const host = document.getElementById("editor");
const state = document.getElementById("state");
let view = null;

function publish() {
  const selection = view.state.selection.main;
  state.textContent = JSON.stringify({
    doc: view.state.doc.toString(),
    anchor: selection.anchor,
    head: selection.head
  });
}

view = new EditorView({
  state: EditorState.create({
    doc: "seed",
    selection: { anchor: 4 },
    extensions: [
      history(),
      keymap.of([...defaultKeymap, ...historyKeymap]),
      EditorView.updateListener.of((update) => {
        if (update.docChanged || update.selectionSet) publish();
      })
    ]
  }),
  parent: host
});
publish();

function destroy_editor() {
  if (!view) return;
  // The host owns the view lifetime; avoid a later update listener publishing
  // state from a CodeMirror instance that has already been detached.
  view.destroy();
  view = null;
  state.setAttribute("data-destroyed", "true");
}

document.getElementById("destroy").addEventListener("click", destroy_editor);
window.__editableProbe = {
  read() { return state.textContent; },
  destroy: destroy_editor
};
