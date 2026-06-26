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

function selectText(root, start, end) {
  const text = firstTextNode(root);
  selection.setBaseAndExtent(text, start, text, end);
}

function clipboardRecord() {
  const records = __lambda_clipboard_read_records();
  return records.length ? records[0] : {};
}

function runCopy() {
  __lambda_clipboard_clear();
  editor.innerHTML = "a<b>bc</b>d";
  selectText(editor.querySelector("b"), 0, 2);

  const supported = document.queryCommandSupported("copy");
  const enabled = document.queryCommandEnabled("copy");
  const ok = document.execCommand("copy");
  const rec = clipboardRecord();

  console.log(
    "copy" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " plain=" + rec["text/plain"] +
    " html=" + rec["text/html"] +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      rec["text/plain"] === "bc" &&
      rec["text/html"] === "<b>bc</b>" &&
      editor.innerHTML === "a<b>bc</b>d"));
}

function runCut() {
  __lambda_clipboard_clear();
  editor.innerHTML = "abcd";
  selectText(editor, 1, 3);

  const supported = document.queryCommandSupported("cut");
  const enabled = document.queryCommandEnabled("cut");
  const ok = document.execCommand("cut");
  const rec = clipboardRecord();

  console.log(
    "cut" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " plain=" + rec["text/plain"] +
    " html=" + rec["text/html"] +
    " editor=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      rec["text/plain"] === "bc" &&
      rec["text/html"] === "bc" &&
      editor.innerHTML === "ad"));
}

function runPasteText() {
  __lambda_clipboard_write_records([{ "text/plain": "XY" }]);
  editor.innerHTML = "ad";
  selectText(editor, 1, 1);

  const supported = document.queryCommandSupported("paste");
  const enabled = document.queryCommandEnabled("paste");
  const ok = document.execCommand("paste");

  console.log(
    "paste-text" +
    " supported=" + supported +
    " enabled=" + enabled +
    " ok=" + ok +
    " editor=" + editor.innerHTML +
    " expected=" + (
      supported &&
      enabled &&
      ok &&
      editor.innerHTML === "aXYd"));
}

function runPasteEmpty() {
  __lambda_clipboard_clear();
  editor.innerHTML = "ad";
  selectText(editor, 1, 1);

  const ok = document.execCommand("paste");

  console.log(
    "paste-empty" +
    " ok=" + ok +
    " editor=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "ad"));
}

runCopy();
runCut();
runPasteText();
runPasteEmpty();
