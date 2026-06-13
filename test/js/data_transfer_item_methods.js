var dt = new DataTransfer();
var textItem = dt.items.add("hello", "Text");
var file = new File(["abc"], "note.txt", { type: "TEXT/PLAIN" });
var fileItem = dt.items.add(file);

var callbackValue = "unset";
var stringReturn = textItem.getAsString(function(value) {
  callbackValue = value;
});

var skippedFileString = "unchanged";
var fileStringReturn = fileItem.getAsString(function(value) {
  skippedFileString = value;
});

var item0 = dt.items.item(0);
var item1 = dt.items.item(1);
var file0 = dt.files.item(0);
var fileFromItem = item1.getAsFile();

console.log("items: " + dt.items.length + "|" + item0.kind + "|" +
  item0.type + "|" + callbackValue + "|" + skippedFileString);
console.log("files: " + dt.files.length + "|" + file0.name + "|" +
  fileFromItem.name + "|" + fileFromItem.type + "|" + fileFromItem.size);
console.log("types: " + dt.types.join(","));
console.log("nulls: " + (textItem.getAsFile() === null) + "|" +
  (dt.items.item(9) === null) + "|" + (dt.files.item(9) === null));
console.log("returns: " + (stringReturn === undefined) + "|" +
  (fileStringReturn === undefined));
