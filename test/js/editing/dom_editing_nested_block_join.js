const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, html, caretText, collapseOffset, expectedHtml,
             expectedStartNode, expectedStartOffset,
             expectedEndNode, expectedEndOffset,
             expectedFocusNode, expectedFocusOffset) {
  editor.innerHTML = html;
  const text = caretText();
  selection.collapse(text, collapseOffset);

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
  "child-block-to-parent",
  "<div><p>abc</p>def</div>",
  () => editor.firstChild.lastChild,
  0,
  "<div><p>abcdef</p></div>",
  () => editor.querySelector("p").firstChild,
  3,
  () => editor.firstChild.lastChild,
  0,
  () => editor.querySelector("p").firstChild,
  3);

run(
  "child-block-to-parent-whitespace",
  "<div><p>abc   </p>   def</div>",
  () => editor.firstChild.lastChild,
  3,
  "<div><p>abcdef</p></div>",
  () => editor.querySelector("p").firstChild,
  3,
  () => editor.firstChild.lastChild,
  3,
  () => editor.querySelector("p").firstChild,
  3);

run(
  "parent-text-to-child-block",
  "<div>abc<p>def</p></div>",
  () => editor.querySelector("p").firstChild,
  0,
  "<div>abcdef</div>",
  () => editor.firstChild.firstChild,
  3,
  () => editor.querySelector("p").firstChild,
  0,
  () => editor.firstChild.firstChild,
  3);

run(
  "parent-text-to-child-block-whitespace",
  "<div>abc   <p>   def</p></div>",
  () => editor.querySelector("p").firstChild,
  3,
  "<div>abcdef</div>",
  () => editor.firstChild.firstChild,
  3,
  () => editor.querySelector("p").firstChild,
  3,
  () => editor.firstChild.firstChild,
  3);
