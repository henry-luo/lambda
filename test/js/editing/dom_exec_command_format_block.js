const editor = document.getElementById("editor");
const selection = getSelection();

function selectText(node, start, end) {
  selection.setBaseAndExtent(node, start, node, end);
}

function runBlock(value, html, expectedHtml, expectedBefore, expectedAfter) {
  editor.innerHTML = html;
  const text = editor.firstChild.firstChild;
  selectText(text, 1, 2);

  const supported = document.queryCommandSupported("formatBlock");
  const enabled = document.queryCommandEnabled("formatBlock");
  const before = document.queryCommandValue("formatBlock");
  const ok = document.execCommand("formatBlock", false, value);
  const after = document.queryCommandValue("formatBlock");

  console.log(
    "formatBlock value=" + value +
    " supported=" + supported +
    " enabled=" + enabled +
    " before=" + before +
    " ok=" + ok +
    " after=" + after +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " valueExpected=" + (before === expectedBefore && after === expectedAfter));
}

function runUnsupported(value) {
  editor.innerHTML = "<p>abc</p>";
  selectText(editor.firstChild.firstChild, 1, 2);
  const ok = document.execCommand("formatBlock", false, value);
  console.log(
    "formatBlock unsupported=" + value +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (!ok && editor.innerHTML === "<p>abc</p>"));
}

runBlock("h1", "<p>abc</p><p>def</p>", "<h1>abc</h1><p>def</p>", "p", "h1");
runBlock("<blockquote>", "<div>abc</div>", "<blockquote>abc</blockquote>", "div", "blockquote");
runUnsupported("span");
