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

function runNoJoin(label, html, caretText, collapseOffset, expectedHtml,
                   expectedFocusNode, expectedFocusOffset) {
  editor.innerHTML = html;
  const text = caretText();
  selection.collapse(text, collapseOffset);

  let beforeRange = "none";
  let inputRanges = "unset";

  function onBeforeInput(event) {
    const ranges = event.getTargetRanges();
    beforeRange = event.inputType + ":" + ranges.length;
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
  "table-cell-text",
  "<table><tr><td>abc</td><td>def</td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  "<table><tbody><tr><td>abcdef</td></tr></tbody></table>",
  () => editor.querySelector("td").firstChild,
  3,
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  () => editor.querySelector("td").firstChild,
  3);

run(
  "table-cell-whitespace",
  "<table><tr><td>abc   </td><td>   def</td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  3,
  "<table><tbody><tr><td>abcdef</td></tr></tbody></table>",
  () => editor.querySelector("td").firstChild,
  3,
  () => editor.querySelectorAll("td")[1].firstChild,
  3,
  () => editor.querySelector("td").firstChild,
  3);

run(
  "table-cell-inline-fragment",
  "<table><tr><td><b>ab</b><i>c</i></td><td><em>d</em><u>ef</u></td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild.firstChild,
  0,
  "<table><tbody><tr><td><b>ab</b><i>c</i><em>d</em><u>ef</u></td></tr></tbody></table>",
  () => editor.querySelector("td").childNodes[1].firstChild,
  1,
  () => editor.querySelectorAll("td")[1].firstChild.firstChild,
  0,
  () => editor.querySelector("td").childNodes[1].firstChild,
  1);

run(
  "table-cell-prev-colspan",
  "<table><tr><td colspan=\"2\">abc</td><td>def</td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  "<table><tbody><tr><td colspan=\"3\">abcdef</td></tr></tbody></table>",
  () => editor.querySelector("td").firstChild,
  3,
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  () => editor.querySelector("td").firstChild,
  3);

run(
  "table-cell-current-colspan",
  "<table><tr><td>abc</td><td colspan=\"2\">def</td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  "<table><tbody><tr><td colspan=\"3\">abcdef</td></tr></tbody></table>",
  () => editor.querySelector("td").firstChild,
  3,
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  () => editor.querySelector("td").firstChild,
  3);

runNoJoin(
  "table-cell-current-rowspan",
  "<table><tr><td>abc</td><td rowspan=\"2\">def</td></tr><tr><td>ghi</td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  "<table><tbody><tr><td>abc</td><td rowspan=\"2\">def</td></tr><tr><td>ghi</td></tr></tbody></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  0);

run(
  "table-cell-cross-row",
  "<table><tr><td>abc</td></tr><tr><td>def</td></tr></table>",
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  "<table><tbody><tr><td>abcdef</td></tr></tbody></table>",
  () => editor.querySelector("td").firstChild,
  3,
  () => editor.querySelectorAll("td")[1].firstChild,
  0,
  () => editor.querySelector("td").firstChild,
  3);
