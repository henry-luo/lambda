const editor = document.getElementById("editor");
const selection = getSelection();

function selectText(node, start, end) {
  selection.setBaseAndExtent(node, start, node, end);
}

function runIndent() {
  editor.innerHTML = "<p>abc</p><p>def</p>";
  selectText(editor.firstChild.firstChild, 1, 2);

  const supported = document.queryCommandSupported("indent");
  const enabled = document.queryCommandEnabled("indent");
  const state = document.queryCommandState("indent");
  const ok = document.execCommand("indent", false, null);

  console.log(
    "indent" +
    " supported=" + supported +
    " enabled=" + enabled +
    " state=" + state +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "<blockquote><p>abc</p></blockquote><p>def</p>"));
}

function runOutdent() {
  editor.innerHTML = "<blockquote><p>abc</p></blockquote><p>def</p>";
  selectText(editor.firstChild.firstChild.firstChild, 1, 2);

  const supported = document.queryCommandSupported("outdent");
  const enabled = document.queryCommandEnabled("outdent");
  const state = document.queryCommandState("outdent");
  const ok = document.execCommand("outdent", false, null);

  console.log(
    "outdent" +
    " supported=" + supported +
    " enabled=" + enabled +
    " state=" + state +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "<p>abc</p><p>def</p>"));
}

runIndent();
runOutdent();
