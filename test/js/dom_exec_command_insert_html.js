var editor = document.getElementById("editor");
var range = document.createRange();
range.setStart(editor.firstChild, 4);
range.collapse(true);
var selection = document.getSelection();
selection.removeAllRanges();
selection.addRange(range);

var observer = new MutationObserver(function () {});
observer.observe(editor, {childList: true, subtree: true});
console.log("callable:" + (typeof document.execCommand));
console.log("inserted:" + document.execCommand("insertHTML", false, "<b>-html</b>"));
var records = observer.takeRecords();
console.log("text:" + editor.textContent);
console.log("bold:" + editor.querySelector("b").textContent);
console.log("notified:" + records.some(function (record) {
  return record.type === "childList";
}));
console.log("collapsed:" + selection.isCollapsed);
console.log("unsupported:" + document.execCommand("bold", false, ""));
