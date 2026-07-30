import EditorJS from "@editorjs/editorjs";
import Paragraph from "@editorjs/paragraph";
import Header from "@editorjs/header";
import ImageTool from "@editorjs/image";
import List from "@editorjs/list";

const state = document.getElementById("state");
let editor = null;
const initial_data = {
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

async function publish() {
  const active_editor = editor;
  if (!active_editor) return;
  const saved = await active_editor.save();
  // Editor.js onChange can complete after reload or destroy. Do not let an
  // obsolete callback overwrite the state published by the current instance.
  if (active_editor == editor) state.textContent = JSON.stringify(saved);
}

async function create_editor(data) {
  editor = new EditorJS({
    holder: "editor",
    autofocus: true,
    tools: {
      paragraph: Paragraph,
      header: Header,
      list: List,
      image: ImageTool
    },
    data,
    onChange: publish
  });
  await editor.isReady;
  await publish();
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
  document.getElementById("destroy").addEventListener("click", destroy_editor);
  document.getElementById("reload").addEventListener("click", reload_editor);
  window.__editableProbe = {
    read() { return state.textContent; },
    destroy: destroy_editor,
    reload: reload_editor
  };
});
