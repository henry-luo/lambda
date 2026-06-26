const editor = document.getElementById("editor");
const selection = getSelection();
const color = "#123456";

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

function runForeColor() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);

  const supported = document.queryCommandSupported("foreColor");
  const enabled = document.queryCommandEnabled("foreColor");
  const ok = document.execCommand("foreColor", false, color);
  const value = document.queryCommandValue("foreColor");

  console.log(
    "foreColor" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " value=" + value +
    " html=" + editor.innerHTML +
    " expected=" + (
      value === color &&
      editor.innerHTML === "a<span style=\"color: " + color + "\">bc</span>d"));
}

function runEmptyColor() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);

  const ok = document.execCommand("foreColor", false, "");

  console.log(
    "foreColor-empty" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "abcd"));
}

runForeColor();
runEmptyColor();
