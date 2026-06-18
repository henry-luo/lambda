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
  "after-inline",
  "<p>a<span>b</span>c</p>",
  () => editor.firstChild.childNodes[2],
  0,
  "<p>ac</p>",
  () => editor.firstChild,
  1,
  () => editor.firstChild.childNodes[2],
  0,
  () => editor.firstChild,
  1);

run(
  "inside-inline",
  "<p>a<span>b</span>c</p>",
  () => editor.querySelector("span").firstChild,
  1,
  "<p>ac</p>",
  () => editor.firstChild,
  1,
  () => editor.firstChild,
  2,
  () => editor.firstChild,
  1);
