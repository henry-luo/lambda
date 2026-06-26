const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, html, expectedHtml,
             expectedStartNode, expectedStartOffset,
             expectedEndNode, expectedEndOffset,
             expectedFocusNode, expectedFocusOffset) {
  editor.innerHTML = html;
  const def = editor.lastChild.firstChild;
  selection.collapse(def, 0);

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
  "one-br",
  "<p>abc<br></p><p>def</p>",
  "<p>abcdef</p>",
  () => editor.firstChild,
  1,
  () => editor.lastChild.firstChild,
  0,
  () => editor.firstChild.firstChild,
  3);

run(
  "two-br",
  "<p>abc<br><br></p><p>def</p>",
  "<p>abc<br>def</p>",
  () => editor.firstChild,
  2,
  () => editor.lastChild.firstChild,
  0,
  () => editor.firstChild.lastChild,
  0);
