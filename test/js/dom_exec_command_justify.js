const editor = document.getElementById("editor");
const selection = getSelection();

function selectText(node, start, end) {
  selection.setBaseAndExtent(node, start, node, end);
}

function runJustify(command, expectedAlign) {
  editor.innerHTML = "<p>abc</p>";
  selectText(editor.firstChild.firstChild, 1, 2);

  const supported = document.queryCommandSupported(command);
  const enabled = document.queryCommandEnabled(command);
  const before = document.queryCommandState(command);
  const ok = document.execCommand(command, false, null);
  const after = document.queryCommandState(command);
  const left = document.queryCommandState("justifyLeft");
  const center = document.queryCommandState("justifyCenter");
  const right = document.queryCommandState("justifyRight");
  const full = document.queryCommandState("justifyFull");
  const expectedHtml = "<p align=\"" + expectedAlign + "\">abc</p>";

  console.log(
    command +
    " supported=" + supported +
    " enabled=" + enabled +
    " before=" + before +
    " ok=" + ok +
    " after=" + after +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " states=" + left + "," + center + "," + right + "," + full);
}

runJustify("justifyLeft", "left");
runJustify("justifyCenter", "center");
runJustify("justifyRight", "right");
runJustify("justifyFull", "justify");
