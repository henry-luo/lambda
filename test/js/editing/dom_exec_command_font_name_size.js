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

function selectMiddle() {
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, 1, text, 3);
}

function runStyleCommand(command, value, property) {
  editor.innerHTML = "abcd";
  selectMiddle();

  const supported = document.queryCommandSupported(command);
  const enabled = document.queryCommandEnabled(command);
  const ok = document.execCommand(command, false, value);
  const queryValue = document.queryCommandValue(command);

  console.log(
    command +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " value=" + queryValue +
    " html=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      queryValue === value &&
      editor.innerHTML === "a<span style=\"" + property + ": " + value + "\">bc</span>d"));
}

function runEmptyStyleCommand(command) {
  editor.innerHTML = "abcd";
  selectMiddle();

  const ok = document.execCommand(command, false, "");

  console.log(
    command + "-empty" +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "abcd"));
}

runStyleCommand("fontName", "serif", "font-family");
runEmptyStyleCommand("fontName");
runStyleCommand("fontSize", "18px", "font-size");
runEmptyStyleCommand("fontSize");
