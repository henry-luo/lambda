// @document dom_input_value_types.html
var transfer = new DataTransfer();
var files = transfer.files;
console.log(files instanceof FileList, files.length);
transfer.items.add(new File(["abc"], "sample.txt", {type: "text/plain"}));
console.log(files.length, files.item(0).name);
transfer.items.add("hello", "text/plain");
console.log(files === transfer.files, files.length, transfer.items.length,
    transfer.items[0].getAsFile().name, transfer.types.join(","));
