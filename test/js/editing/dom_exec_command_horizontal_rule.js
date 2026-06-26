const editor = document.getElementById("editor");
const selection = getSelection();

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

function runCollapsedInsert() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 2, text, 2);

  const supported = document.queryCommandSupported("insertHorizontalRule");
  const enabled = document.queryCommandEnabled("insertHorizontalRule");
  const ok = document.execCommand("insertHorizontalRule", false, null);

  console.log(
    "insertHorizontalRule-collapsed" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "ab<hr>cd"));
}

function runSelectedInsert() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);

  const ok = document.execCommand("insertHorizontalRule", false, null);

  console.log(
    "insertHorizontalRule-selected" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "a<hr>d"));
}

runCollapsedInsert();
runSelectedInsert();
