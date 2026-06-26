const editor = document.getElementById("editor");
const selection = getSelection();
let helperCalls = 0;

globalThis.__lambda_execCommand_handler = function(command, showUI, value) {
  helperCalls++;
  const name = String(command || "").toLowerCase();
  if (name === "delete" || name === "forwarddelete") {
    return document.execCommand(command, showUI || false, value);
  }
  return false;
};

function run(label, command, html, offset, expectedHtml, expectedOffset) {
  editor.innerHTML = html;
  selection.collapse(editor.firstChild, offset);

  const ok = document.execCommand(command, false, null);

  console.log(
    label +
    " ok=" + ok +
    " html=" + JSON.stringify(editor.innerHTML) +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " focus=" + (selection.focusNode === editor.firstChild) +
    ":" + selection.focusOffset +
    " focusExpected=" + expectedOffset);
}

run("delete-fallback", "delete", "foo", 2, "fo", 1);
run("forwarddelete-fallback", "forwardDelete", "foo", 1, "fo", 1);
console.log("helper-calls=" + helperCalls);
