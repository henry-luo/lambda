const editor = document.getElementById("editor");
const selection = getSelection();
const color = "#fedcba";

function selectedTextNode() {
  function walk(node) {
    if (!node) {
      return null;
    }
    if (node.nodeType === 3) {
      return node;
    }
    for (let child = node.firstChild; child; child = child.nextSibling) {
      const found = walk(child);
      if (found) {
        return found;
      }
    }
    return null;
  }
  return walk(editor);
}

function selectMiddle() {
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);
}

function runColorCommand(command) {
  editor.innerHTML = "abcd";
  selectMiddle();

  const supported = document.queryCommandSupported(command);
  const enabled = document.queryCommandEnabled(command);
  const ok = document.execCommand(command, false, color);
  const value = document.queryCommandValue(command);

  console.log(
    command +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " value=" + value +
    " html=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      value === color &&
      editor.innerHTML === "a<span style=\"background-color: " + color + "\">bc</span>d"));
}

function runEmptyColor(command) {
  editor.innerHTML = "abcd";
  selectMiddle();

  const ok = document.execCommand(command, false, "");

  console.log(
    command + "-empty" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "abcd"));
}

runColorCommand("backColor");
runEmptyColor("backColor");
runColorCommand("hiliteColor");
runEmptyColor("hiliteColor");
