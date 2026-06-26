const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, html, collapseNode, collapseOffset, expectedHtml,
             expectedStartNode, expectedStartOffset,
             expectedEndNode, expectedEndOffset,
             expectedFocusNode, expectedFocusOffset) {
  editor.innerHTML = html;
  const node = collapseNode();
  selection.collapse(node, collapseOffset);

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
  "br-before-text",
  "<p>abc<br>def</p>",
  () => editor.querySelector("br").nextSibling,
  0,
  "<p>abcdef</p>",
  () => editor.firstChild,
  1,
  () => editor.firstChild,
  2,
  () => editor.firstChild,
  1);

run(
  "img-before-text",
  '<p>abc<img src="x">def</p>',
  () => editor.querySelector("img").nextSibling,
  0,
  '<p>abcdef</p>',
  () => editor.firstChild,
  1,
  () => editor.firstChild,
  2,
  () => editor.firstChild,
  1);

run(
  "img-boundary",
  '<p>abc<img src="x"><img src="y">def</p>',
  () => editor.firstChild,
  2,
  '<p>abc<img src="y">def</p>',
  () => editor.firstChild,
  1,
  () => editor.firstChild,
  2,
  () => editor.firstChild,
  1);

run(
  "hr-before-text",
  "<div>abc<hr>def</div>",
  () => editor.querySelector("hr").nextSibling,
  0,
  "<div>abcdef</div>",
  () => editor.firstChild,
  1,
  () => editor.firstChild,
  2,
  () => editor.firstChild,
  1);

run(
  "br-leading-space-start",
  "<p>abc<br> def</p>",
  () => editor.querySelector("br").nextSibling,
  0,
  "<p>abcdef</p>",
  () => editor.firstChild,
  1,
  () => editor.querySelector("br").nextSibling,
  1,
  () => editor.firstChild,
  1);

run(
  "br-leading-space-inside",
  "<p>abc<br> def</p>",
  () => editor.querySelector("br").nextSibling,
  1,
  "<p>abcdef</p>",
  () => editor.firstChild,
  1,
  () => editor.querySelector("br").nextSibling,
  1,
  () => editor.firstChild,
  1);

run(
  "hr-trailing-space",
  "<div>abc <hr>def</div>",
  () => editor.querySelector("hr").nextSibling,
  0,
  "<div>abcdef</div>",
  () => editor.firstChild.firstChild,
  3,
  () => editor.firstChild,
  2,
  () => editor.firstChild,
  1);

run(
  "hr-leading-space",
  "<div>abc<hr> def</div>",
  () => editor.querySelector("hr").nextSibling,
  1,
  "<div>abcdef</div>",
  () => editor.firstChild,
  1,
  () => editor.querySelector("hr").nextSibling,
  1,
  () => editor.firstChild,
  1);

run(
  "br-before-hr",
  "<div>abc<br><hr>def</div>",
  () => editor.firstChild.lastChild,
  0,
  "<div>abcdef</div>",
  () => editor.firstChild,
  1,
  () => editor.firstChild,
  3,
  () => editor.firstChild,
  1);
