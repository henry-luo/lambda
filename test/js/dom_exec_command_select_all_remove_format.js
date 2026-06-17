const editor = document.getElementById("editor");
const selection = getSelection();

function firstTextNode(root) {
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
  return walk(root);
}

function selectElementContents(element) {
  selection.setBaseAndExtent(element, 0, element, element.childNodes.length);
}

function runSelectAll() {
  editor.innerHTML = "ab<b>cd</b>ef";
  const text = firstTextNode(editor);
  selection.setBaseAndExtent(text, 1, text, 1);

  const supported = document.queryCommandSupported("selectAll");
  const enabled = document.queryCommandEnabled("selectAll");
  const ok = document.execCommand("selectAll");
  const selectedAll =
    selection.anchorNode === editor &&
    selection.anchorOffset === 0 &&
    selection.focusNode === editor &&
    selection.focusOffset === editor.childNodes.length;

  console.log(
    "selectAll" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " selectedAll=" + selectedAll +
    " html=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      selectedAll &&
      editor.innerHTML === "ab<b>cd</b>ef"));
}

function runRemoveFormatNested() {
  editor.innerHTML = "a<b><i>bc</i></b>d";
  const italic = editor.querySelector("i");
  selectElementContents(italic);

  const supported = document.queryCommandSupported("removeFormat");
  const enabled = document.queryCommandEnabled("removeFormat");
  const ok = document.execCommand("removeFormat");

  console.log(
    "removeFormat-nested" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      editor.innerHTML === "abcd"));
}

function runRemoveFormatStyle() {
  editor.innerHTML = "a<span style=\"color: #123456\">bc</span>d";
  const span = editor.querySelector("span");
  selectElementContents(span);

  const ok = document.execCommand("removeFormat");

  console.log(
    "removeFormat-style" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (ok && editor.innerHTML === "abcd"));
}

function runRemoveFormatCollapsed() {
  editor.innerHTML = "a<b>bc</b>d";
  const text = firstTextNode(editor.querySelector("b"));
  selection.setBaseAndExtent(text, 1, text, 1);

  const ok = document.execCommand("removeFormat");

  console.log(
    "removeFormat-collapsed" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "a<b>bc</b>d"));
}

runSelectAll();
runRemoveFormatNested();
runRemoveFormatStyle();
runRemoveFormatCollapsed();
