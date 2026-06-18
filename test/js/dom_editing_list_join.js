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
  "list-item-text",
  "<ol><li>abc</li><li>def</li></ol>",
  () => editor.querySelectorAll("li")[1].firstChild,
  0,
  "<ol><li>abcdef</li></ol>",
  () => editor.querySelector("li").firstChild,
  3,
  () => editor.querySelectorAll("li")[1].firstChild,
  0,
  () => editor.querySelector("li").firstChild,
  3);

run(
  "list-item-whitespace",
  "<ul><li>abc   </li><li>   def</li></ul>",
  () => editor.querySelectorAll("li")[1].firstChild,
  3,
  "<ul><li>abcdef</li></ul>",
  () => editor.querySelector("li").firstChild,
  3,
  () => editor.querySelectorAll("li")[1].firstChild,
  3,
  () => editor.querySelector("li").firstChild,
  3);

run(
  "list-item-inline-fragment",
  "<ul><li><b>ab</b><i>c</i></li><li><em>d</em><u>ef</u></li></ul>",
  () => editor.querySelectorAll("li")[1].firstChild.firstChild,
  0,
  "<ul><li><b>ab</b><i>c</i><em>d</em><u>ef</u></li></ul>",
  () => editor.querySelector("li").childNodes[1].firstChild,
  1,
  () => editor.querySelectorAll("li")[1].firstChild.firstChild,
  0,
  () => editor.querySelector("li").childNodes[1].firstChild,
  1);
