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

function runCreateLink() {
  editor.innerHTML = "abcd";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);

  const supported = document.queryCommandSupported("createLink");
  const enabled = document.queryCommandEnabled("createLink");
  const ok = document.execCommand("createLink", false, "https://example.test/");

  console.log(
    "createLink" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "a<a href=\"https://example.test/\">bc</a>d"));
}

function runUnlink() {
  editor.innerHTML = "<a href=\"https://example.test/\">bc</a>";
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 0, text, 2);

  const supported = document.queryCommandSupported("unlink");
  const enabled = document.queryCommandEnabled("unlink");
  const ok = document.execCommand("unlink", false, null);

  console.log(
    "unlink" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === "bc"));
}

runCreateLink();
runUnlink();
