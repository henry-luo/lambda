// @document dom_module_props.html
var main = document.getElementById("main");
var comment = document.createComment("note");
main.appendChild(comment);
var nodes = main.childNodes;
var tail = document.createElement("span");
main.appendChild(tail);

console.log("fresh:" + main.childNodes.length);
console.log("live:" + nodes.length);
console.log("tail:" + (nodes[2] === tail));
console.log("array:" + Array.isArray(nodes));
console.log("nodeList:" + (nodes instanceof NodeList));
var children = main.children;
console.log("childrenArray:" + Array.isArray(children));
console.log("htmlCollection:" + (children instanceof HTMLCollection));
