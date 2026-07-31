import EditorJS from "@editorjs/editorjs";
import Paragraph from "@editorjs/paragraph";
import Header from "@editorjs/header";
import ImageTool from "@editorjs/image";
import List from "@editorjs/list";

const state = document.getElementById("state");
const changes = document.getElementById("changes");
const holder = document.getElementById("editor");
const selection_output = document.getElementById("selection");
const ids_output = document.getElementById("ids");
const is_read_only = holder.hasAttribute("data-readonly");
const is_single_block = holder.getAttribute("data-editor-mode") === "single-block";
const starts_empty = holder.hasAttribute("data-initial-empty");
// manual pages can defer Editor.js focus so its block toolbox opens only after
// the user intentionally enters an editable block.
const autofocus = !holder.hasAttribute("data-no-autofocus");
const initial_text = holder.getAttribute("data-doc") || "seed";
let editor = null;
const change_log = [];
changes.textContent = JSON.stringify(change_log);
const initial_data = starts_empty ? { blocks: [] } : is_single_block ? {
  blocks: [
    { type: "paragraph", data: { text: initial_text } }
  ]
} : {
  blocks: [
    { type: "paragraph", data: { text: "seed" } },
    { type: "header", data: { text: "heading", level: 2 } },
    { type: "list", data: { style: "unordered", items: ["list-item"] } },
    {
      type: "image",
      data: {
        file: { url: "data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/%3E" },
        caption: "atomic-media",
        withBorder: false,
        withBackground: false,
        stretched: false
      }
    }
  ]
};

function publish_block_ids() {
  if (!ids_output) return;
  const ids = Array.from(holder.querySelectorAll(".ce-block"))
    .map((block) => block.getAttribute("data-id") || "");
  ids_output.textContent = ids.join(",");
  ids_output.setAttribute("data-count", String(ids.length));
  ids_output.setAttribute("data-unique", String(
    ids.length > 0 && !ids.includes("") && new Set(ids).size === ids.length
  ));
}

async function publish() {
  const active_editor = editor;
  if (!active_editor) return;
  const saved = await active_editor.save();
  // Editor.js onChange can complete after reload or destroy. Do not let an
  // obsolete callback overwrite the state published by the current instance.
  if (active_editor == editor) {
    state.textContent = JSON.stringify(saved);
    publish_block_ids();
  }
}

function record_change(change) {
  const batch = Array.isArray(change) ? change : [change];
  for (const entry of batch) {
    if (!entry) continue;
    const detail = entry.detail || {};
    const target = detail.target || {};
    change_log.push({
      type: entry.type || "",
      index: typeof detail.index === "number" ? detail.index : null,
      target: target.name || ""
    });
  }
  changes.textContent = JSON.stringify(change_log);
  publish_block_ids();
}

function publish_selection() {
  if (!selection_output) return;
  const selection = document.getSelection();
  const node = selection && selection.focusNode;
  const element = node && (node.nodeType === 3 ? node.parentElement : node);
  const input = element && element.closest("[contenteditable=true]");
  selection_output.textContent = input
    ? `${input.textContent}:${selection.focusOffset}`
    : "";
}

async function create_editor(data) {
  editor = new EditorJS({
    holder: "editor",
    autofocus,
    readOnly: is_read_only,
    tools: {
      paragraph: Paragraph,
      header: Header,
      list: List,
      image: ImageTool
    },
    data,
    onChange(api, change) {
      record_change(change);
      publish();
    }
  });
  await editor.isReady;
  // Editor.js intentionally rejects Saver.save() in read-only mode, so expose
  // the configured source state while the fixture verifies it cannot change.
  if (is_read_only) state.textContent = JSON.stringify(data);
  else await publish();
}

async function destroy_editor() {
  if (!editor) return;
  const active_editor = editor;
  editor = null;
  await active_editor.destroy();
  state.setAttribute("data-destroyed", "true");
}

async function reload_editor() {
  const active_editor = editor;
  const saved = active_editor ? await active_editor.save() : initial_data;
  await destroy_editor();
  state.removeAttribute("data-destroyed");
  await create_editor(saved);
  state.setAttribute("data-reloaded", "true");
}

create_editor(initial_data).then(() => {
  document.addEventListener("selectionchange", publish_selection);
  publish_selection();
  publish_block_ids();
  document.getElementById("destroy").addEventListener("click", destroy_editor);
  document.getElementById("reload").addEventListener("click", reload_editor);
  window.__editableProbe = {
    read() { return state.textContent; },
    destroy: destroy_editor,
    reload: reload_editor
  };
});
