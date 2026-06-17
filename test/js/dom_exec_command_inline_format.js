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

function runFormat(command, html, start, end, expectedHtml, expectedState) {
  editor.innerHTML = html;
  const text = selectedTextNode();
  selection.setBaseAndExtent(text, start, text, end);

  const supported = document.queryCommandSupported(command);
  const enabled = document.queryCommandEnabled(command);
  const ok = document.execCommand(command, false, null);
  const state = document.queryCommandState(command);

  console.log(
    command +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " state=" + state +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " stateExpected=" + (state === expectedState));
}

function runState(command, html, textGetter, expectedState) {
  editor.innerHTML = html;
  const text = textGetter();
  selection.collapse(text, 1);
  const state = document.queryCommandState(command);
  console.log(
    command +
    "-state state=" + state +
    " expected=" + (state === expectedState));
}

runFormat("strikethrough", "abcd", 1, 3, "a<s>bc</s>d", true);
runFormat("subscript", "abcd", 1, 3, "a<sub>bc</sub>d", true);
runFormat("superscript", "abcd", 1, 3, "a<sup>bc</sup>d", true);

runState("bold", "<strong>ab</strong>", () => editor.firstChild.firstChild, true);
runState("italic", "<em>ab</em>", () => editor.firstChild.firstChild, true);
runState("strikethrough", "<strike>ab</strike>", () => editor.firstChild.firstChild, true);

runFormat("bold", "<b>bc</b>", 0, 2, "bc", false);
runFormat("bold", "<strong>bc</strong>", 0, 2, "bc", false);
runFormat("italic", "<em>bc</em>", 0, 2, "bc", false);
runFormat("strikethrough", "<strike>bc</strike>", 0, 2, "bc", false);
