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
console.log(text.parentNode === document.getElementById("intro"));
console.log(text.parentElement === document.getElementById("intro"));
console.log(text.firstChild === null);
console.log(text.childNodes.length);

var comment = document.createComment("note");
main.appendChild(comment);
console.log(comment.nodeType);
console.log(comment.nodeName);
console.log(comment.data);
console.log(comment.length);
console.log(comment.parentNode === main);
console.log(comment.previousSibling === document.getElementById("intro"));
console.log(comment.nextSibling === null);
console.log(main.firstChild === document.getElementById("intro"));
console.log(main.lastChild === comment);
console.log(main.childNodes.length);
console.log(document.getElementById("intro").nextSibling === comment);
console.log(comment.previousSibling.parentNode === main);

main.id = "main-updated";
main.className = "gamma delta";
console.log(main.id);
console.log(main.getAttribute("id"));
console.log(document.getElementById("main-updated") === main);
console.log(main.className);
console.log(main.getAttribute("class"));
