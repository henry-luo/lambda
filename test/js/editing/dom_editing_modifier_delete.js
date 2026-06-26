const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, key, shift, ctrl, alt, meta, html, offset, expectedHtml,
             expectedInputType, expectedStartOffset, expectedEndOffset,
             expectedFocusOffset) {
  editor.innerHTML = html;
  const text = editor.firstChild.firstChild;
  selection.collapse(text, offset);

  let beforeRange = "none";
  let inputRanges = "unset";

  function onBeforeInput(event) {
    const ranges = event.getTargetRanges();
    beforeRange = event.inputType + ":" + ranges.length + ":" +
      (ranges.length ? [
        ranges[0].startContainer === text,
        ranges[0].startOffset,
        ranges[0].endContainer === text,
        ranges[0].endOffset
      ].join(",") : "empty");
  }

  function onInput(event) {
    inputRanges = event.inputType + ":" + event.getTargetRanges().length;
  }

  editor.addEventListener("beforeinput", onBeforeInput);
  editor.addEventListener("input", onInput);
  const ok = __lambda_testdriver_key(key, shift, ctrl, alt, meta);
  editor.removeEventListener("beforeinput", onBeforeInput);
  editor.removeEventListener("input", onInput);

  console.log(
    label +
    " ok=" + ok +
    " html=" + JSON.stringify(editor.innerHTML) +
    " expected=" + (editor.innerHTML === expectedHtml) +
    " before=" + beforeRange +
    " expectedBefore=" + expectedInputType + ":1:true," +
      expectedStartOffset + ",true," + expectedEndOffset +
    " input=" + inputRanges +
    " focus=" + (selection.focusNode === editor.firstChild.firstChild) +
    ":" + selection.focusOffset +
    " focusExpected=" + expectedFocusOffset);
}

run(
  "meta-backspace-line",
  0xE003,
  false,
  false,
  false,
  true,
  "<p>one\ntwo three</p>",
  7,
  "<p>one\n three</p>",
  "deleteSoftLineBackward",
  4,
  7,
  4);

run(
  "meta-delete-line",
  0xE017,
  false,
  false,
  false,
  true,
  "<p>one\ntwo three</p>",
  4,
  "<p>one\n</p>",
  "deleteSoftLineForward",
  4,
  13,
  4);

run(
  "alt-backspace-word",
  0xE003,
  false,
  false,
  true,
  false,
  "<p>one two three</p>",
  13,
  "<p>one two </p>",
  "deleteWordBackward",
  8,
  13,
  8);

run(
  "alt-delete-word",
  0xE017,
  false,
  false,
  true,
  false,
  "<p>one two three</p>",
  4,
  "<p>one  three</p>",
  "deleteWordForward",
  4,
  7,
  4);

run(
  "ctrl-backspace-word",
  0xE003,
  false,
  true,
  false,
  false,
  "<p>one two three</p>",
  13,
  "<p>one two </p>",
  "deleteWordBackward",
  8,
  13,
  8);

run(
  "ctrl-delete-word",
  0xE017,
  false,
  true,
  false,
  false,
  "<p>one two three</p>",
  4,
  "<p>one  three</p>",
  "deleteWordForward",
  4,
  7,
  4);
