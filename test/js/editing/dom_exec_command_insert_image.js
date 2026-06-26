const editor = document.getElementById("editor");
const selection = getSelection();
const src = "https://example.test/image.png";

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

  const supported = document.queryCommandSupported("insertImage");
  const enabled = document.queryCommandEnabled("insertImage");
  const ok = document.execCommand("insertImage", false, src);

  console.log(
    "insertImage-collapsed" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "ab<img src=\"" + src + "\">cd"));
}

function runSelectedInsert() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);

  const ok = document.execCommand("insertImage", false, src);

  console.log(
    "insertImage-selected" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "a<img src=\"" + src + "\">d"));
}

function runEmptySrc() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 2, text, 2);

  const ok = document.execCommand("insertImage", false, "");

  console.log(
    "insertImage-empty-src" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "abcd"));
}

runCollapsedInsert();
runSelectedInsert();
runEmptySrc();
