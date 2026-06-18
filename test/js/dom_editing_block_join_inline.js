const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, html, firstText, secondText, expectedHtml) {
  editor.innerHTML = html;
  const first = firstText();
  const second = secondText();
  selection.collapse(second, 0);

  let beforeRange = "none";
  let inputRanges = "unset";

  function onBeforeInput(event) {
    const ranges = event.getTargetRanges();
    beforeRange = event.inputType + ":" + ranges.length + ":" +
      (ranges.length ? [
        ranges[0].startContainer === first,
        ranges[0].startOffset,
        ranges[0].endContainer === second,
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
    " focus=" + (selection.focusNode === first) +
    ":" + selection.focusOffset);
}

run(
  "text-bold",
  "<p>abc</p><p><b>def</b></p>",
  () => editor.firstChild.firstChild,
  () => editor.firstChild.nextSibling.firstChild.firstChild,
  "<p>abc<b>def</b></p>");

run(
  "bold-bold",
  "<p><b>abc</b></p><p><b>def</b></p>",
  () => editor.firstChild.firstChild.firstChild,
  () => editor.firstChild.nextSibling.firstChild.firstChild,
  "<p><b>abc</b><b>def</b></p>");

run(
  "italic-bold",
  "<p><i>abc</i></p><p><b>def</b></p>",
  () => editor.firstChild.firstChild.firstChild,
  () => editor.firstChild.nextSibling.firstChild.firstChild,
  "<p><i>abc</i><b>def</b></p>");

run(
  "text-nested-inline",
  "<p>abc</p><p><b><i>def</i></b></p>",
  () => editor.firstChild.firstChild,
  () => editor.firstChild.nextSibling.firstChild.firstChild.firstChild,
  "<p>abc<b><i>def</i></b></p>");

run(
  "nested-nested-inline",
  "<p><b><i>abc</i></b></p><p><u><em>def</em></u></p>",
  () => editor.firstChild.firstChild.firstChild.firstChild,
  () => editor.firstChild.nextSibling.firstChild.firstChild.firstChild,
  "<p><b><i>abc</i></b><u><em>def</em></u></p>");
