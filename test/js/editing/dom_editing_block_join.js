const editor = document.getElementById("editor");
const first = editor.firstChild.firstChild;
const second = editor.firstChild.nextSibling.firstChild;
const selection = getSelection();
selection.collapse(second, 0);

let beforeRange = "none";
let inputRanges = "unset";

editor.addEventListener("beforeinput", event => {
  const ranges = event.getTargetRanges();
  beforeRange = event.inputType + ":" + ranges.length + ":" +
    (ranges.length ? [
      ranges[0].startContainer === first,
      ranges[0].startOffset,
      ranges[0].endContainer === second,
      ranges[0].endOffset
    ].join(",") : "empty");
});

editor.addEventListener("input", event => {
  inputRanges = event.inputType + ":" + event.getTargetRanges().length;
});

const ok = __lambda_testdriver_key(0xE003, false, false, false, false);
console.log("ok=" + ok);
console.log("html=" + editor.innerHTML);
console.log("before=" + beforeRange);
console.log("input=" + inputRanges);
console.log("focus=" + (selection.focusNode === editor.firstChild.firstChild) + ":" + selection.focusOffset);
