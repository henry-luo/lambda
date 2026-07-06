// Radiant module-owned DOM property dispatch smoke.

var main = document.getElementById("main");
console.log(main.tagName);
console.log(main.localName);
console.log(main.namespaceURI);
console.log(main.prefix === null);
console.log(main.id);
console.log(main.className);
console.log(main.nodeType);

var text = document.getElementById("intro").firstChild;
console.log(text.nodeType);
console.log(text.nodeName);
console.log(text.nodeValue);
console.log(text.textContent);
console.log(text.length);

var comment = document.createComment("note");
console.log(comment.nodeType);
console.log(comment.nodeName);
console.log(comment.data);
console.log(comment.length);
