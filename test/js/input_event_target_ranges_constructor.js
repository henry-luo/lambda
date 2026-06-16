var startNode = { id: "start" };
var endNode = { id: "end" };
var initRange = {
  startContainer: startNode,
  startOffset: 2,
  endContainer: endNode,
  endOffset: 5
};
var staticRange = new StaticRange(initRange);
var ev = new InputEvent("beforeinput", {
  inputType: "insertText",
  data: "x",
  isComposing: true,
  targetRanges: [staticRange]
});

initRange.startOffset = 99;
initRange.endOffset = 100;

var first = ev.getTargetRanges();
var second = ev.getTargetRanges();
first[0].startOffset = 42;

console.log("surface: " + ev.inputType + "|" + ev.data + "|" +
  (ev.isComposing ? "1" : "0") + "|" +
  (typeof ev.getTargetRanges));
console.log("ranges: " + first.length + "|" + second.length + "|" +
  first[0].startOffset + "-" + first[0].endOffset + "|" +
  second[0].startOffset + "-" + second[0].endOffset);
console.log("fresh: " + (first !== second) + "|" + (first[0] !== second[0]));
console.log("nodes: " + first[0].startContainer.id + "|" +
  first[0].endContainer.id + "|" + first[0].collapsed);

var defaultEvent = new InputEvent("input");
var nullDataEvent = new InputEvent("input", { data: null });
var undefinedDataEvent = new InputEvent("input", { data: undefined });
var emptyDataEvent = new InputEvent("input", { data: "" });
var defaultRangesA = defaultEvent.getTargetRanges();
var defaultRangesB = defaultEvent.getTargetRanges();

console.log("defaults: " + (defaultEvent.data === null) + "|" +
  defaultEvent.inputType + "|" + (defaultEvent.dataTransfer === null) + "|" +
  (defaultEvent.isComposing ? "1" : "0") + "|" +
  defaultRangesA.length + "|" + (defaultRangesA !== defaultRangesB));
console.log("nullable: " + (nullDataEvent.data === null) + "|" +
  (undefinedDataEvent.data === null) + "|" + (emptyDataEvent.data === ""));
