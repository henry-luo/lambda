const editor = document.getElementById("editor");
const selection = getSelection();

function run(offset) {
  editor.innerHTML = "<p>abc   </p><p>   def</p>";
  const first = editor.firstChild.firstChild;
  const second = editor.firstChild.nextSibling.firstChild;
  selection.collapse(second, offset);

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
    "offset=" + offset +
    " ok=" + ok +
    " html=" + editor.innerHTML +
    " before=" + beforeRange +
    " input=" + inputRanges +
    " focus=" + (selection.focusNode === editor.firstChild.firstChild) +
    ":" + selection.focusOffset);
}

run(3);
run(2);
run(1);
run(0);
