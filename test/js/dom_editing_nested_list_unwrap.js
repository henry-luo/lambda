const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, html, caretText, expectedHtml,
             expectedStartNode, expectedStartOffset,
             expectedEndNode, expectedEndOffset,
             expectedFocusNode, expectedFocusOffset) {
  editor.innerHTML = html;
  const text = caretText();
  selection.collapse(text, 0);

  let beforeRange = "none";
  let inputRanges = "unset";

  function onBeforeInput(event) {
    const ranges = event.getTargetRanges();
    const startNode = expectedStartNode();
    const endNode = expectedEndNode();
    beforeRange = event.inputType + ":" + ranges.length + ":" +
      (ranges.length ? [
        ranges[0].startContainer === startNode,
        ranges[0].startOffset,
        ranges[0].endContainer === endNode,
        ranges[0].endOffset
      ].join(",") : "empty");
  }

  function onInput(event) {
    inputRanges = event.inputType + ":" + event.getTargetRanges().length;
  }

  editor.addEventListener("beforeinput", onBeforeInput);
  editor.addEventListener("input", onInput);
  const ok = __lambda_testdriver_key(0xE003, false, false, false, false);
  editor.removeEventListener("beforeinput", onBeforeInput);
  editor.removeEventListener("input", onInput);

  console.log(
    label +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " before=" + beforeRange +
    " input=" + inputRanges +
    " focus=" + (selection.focusNode === expectedFocusNode()) +
    ":" + selection.focusOffset +
    " focusExpected=" + expectedFocusOffset);
}

run(
  "nested-list-text",
  "<ul><li>abc<ul><li>def</li></ul></li></ul>",
  () => editor.querySelectorAll("li")[1].firstChild,
  "<ul><li>abc</li><li>def</li></ul>",
  () => editor.querySelector("li").firstChild,
  3,
  () => editor.querySelectorAll("li")[1].firstChild,
  0,
  () => editor.querySelectorAll("li")[1].firstChild,
  0);

run(
  "nested-list-inline",
  "<ol><li><b>abc</b><ol><li><em>def</em></li></ol></li></ol>",
  () => editor.querySelectorAll("li")[1].firstChild.firstChild,
  "<ol><li><b>abc</b></li><li><em>def</em></li></ol>",
  () => editor.querySelector("b").firstChild,
  3,
  () => editor.querySelector("em").firstChild,
  0,
  () => editor.querySelector("em").firstChild,
  0);

run(
  "nested-list-multi-item",
  "<ul><li>abc<ul><li>def</li><li>ghi</li></ul></li></ul>",
  () => editor.querySelectorAll("li")[1].firstChild,
  "<ul><li>abc<ul><li>ghi</li></ul></li><li>def</li></ul>",
  () => editor.querySelector("li").firstChild,
  3,
  () => editor.querySelectorAll("li")[1].firstChild,
  0,
  () => editor.children[0].children[1].firstChild,
  0);

run(
  "nested-list-trailing-whitespace",
  "<ul><li>abc<ul><li>def</li></ul>  </li></ul>",
  () => editor.querySelectorAll("li")[1].firstChild,
  "<ul><li>abc</li><li>def</li></ul>",
  () => editor.querySelector("li").firstChild,
  3,
  () => editor.querySelectorAll("li")[1].firstChild,
  0,
  () => editor.querySelectorAll("li")[1].firstChild,
  0);
