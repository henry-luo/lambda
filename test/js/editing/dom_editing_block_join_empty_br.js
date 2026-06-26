const editor = document.getElementById("editor");
const selection = getSelection();

function run(label, html, expectedHtml) {
  editor.innerHTML = html;
  const previousBlock = editor.children[0];
  const currentText = editor.children[1].firstChild;
  selection.collapse(currentText, 0);

  let beforeRange = "none";
  let inputRanges = "unset";

  function onBeforeInput(event) {
    const ranges = event.getTargetRanges();
    beforeRange = event.inputType + ":" + ranges.length + ":" +
      (ranges.length ? [
        ranges[0].startContainer === previousBlock,
        ranges[0].startOffset,
        ranges[0].endContainer === currentText,
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
    " focus=" + (selection.focusNode === editor.children[0].firstChild) +
    ":" + selection.focusOffset +
    " focusExpected=0");
}

run(
  "empty-br-paragraph",
  "<p><br></p><p>def</p>",
  "<p>def</p>");

run(
  "empty-br-div",
  "<div><br></div><div>def</div>",
  "<div>def</div>");

run(
  "whitespace-empty-br-paragraph",
  "<p>   <br></p><p>def</p>",
  "<p>def</p>");

run(
  "inline-wrapped-empty-br-paragraph",
  "<p> <span> <br> </span> </p><p>def</p>",
  "<p>def</p>");
