import { EditorState } from "@codemirror/state";
import { Direction, EditorView, keymap } from "@codemirror/view";
import { defaultKeymap, history, historyKeymap } from "@codemirror/commands";

const host = document.getElementById("editor");
const state = document.getElementById("state");
const events = document.getElementById("events");
const direction = document.getElementById("direction");
let view = null;
const event_log = [];
const initial_doc = host.getAttribute("data-doc") || "seed";
const read_only = host.hasAttribute("data-readonly");
const text_direction = host.getAttribute("data-direction");
if (events) events.textContent = JSON.stringify(event_log);

function record_event(event) {
  if (!events) return;
  event_log.push(event.inputType ? `${event.type}:${event.inputType}` : event.type);
  events.textContent = JSON.stringify(event_log);
}

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
    doc: initial_doc,
    // Direction fixtures use short documents; the initial selection must stay
    // within the configured document instead of assuming the default "seed".
    selection: { anchor: initial_doc.length },
    extensions: [
      history(),
      keymap.of([...defaultKeymap, ...historyKeymap]),
      EditorView.domEventHandlers({
        keydown(event) { record_event(event); return false; },
        beforeinput(event) { record_event(event); return false; },
        input(event) { record_event(event); return false; },
        paste(event) { record_event(event); return false; },
        copy(event) { record_event(event); return false; },
        cut(event) { record_event(event); return false; }
      }),
      ...(text_direction ? [EditorView.theme({
        ".cm-content": { direction: text_direction }
      })] : []),
      ...(read_only ? [EditorState.readOnly.of(true)] : []),
      EditorView.updateListener.of((update) => {
        if (update.docChanged || update.selectionSet) publish();
      })
    ]
  }),
  parent: host
});
publish();
if (direction) {
  requestAnimationFrame(() => {
    view.measure();
    direction.textContent = view.textDirection === Direction.RTL ? "rtl" : "ltr";
  });
}

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
