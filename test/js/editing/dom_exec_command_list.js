const editor = document.getElementById("editor");
const selection = getSelection();

function selectText(node, start, end) {
  selection.setBaseAndExtent(node, start, node, end);
}

function runList(command, expectedHtml, expectedOrdered, expectedUnordered) {
  editor.innerHTML = "<p>abc</p><p>def</p>";
  selectText(editor.firstChild.firstChild, 1, 2);

  const supported = document.queryCommandSupported(command);
  const enabled = document.queryCommandEnabled(command);
  const before = document.queryCommandState(command);
  const ok = document.execCommand(command, false, null);
  const after = document.queryCommandState(command);
  const ordered = document.queryCommandState("insertOrderedList");
  const unordered = document.queryCommandState("insertUnorderedList");

  console.log(
    command +
    " supported=" + supported +
    " enabled=" + enabled +
    " before=" + before +
    " ok=" + ok +
    " after=" + after +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " states=" + ordered + "," + unordered +
    " stateExpected=" + (ordered === expectedOrdered && unordered === expectedUnordered));
}

runList("insertOrderedList", "<ol><li>abc</li></ol><p>def</p>", true, false);
runList("insertUnorderedList", "<ul><li>abc</li></ul><p>def</p>", false, true);
