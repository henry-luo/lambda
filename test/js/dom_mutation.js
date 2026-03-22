// DOM Mutation Tests - createElement, appendChild, removeChild, insertBefore, etc.

// --- createElement + appendChild ---
var container = document.getElementById("container");
console.log(container.childElementCount); // 3

var newDiv = document.createElement("div");
newDiv.setAttribute("id", "added");
container.appendChild(newDiv);
console.log(container.childElementCount); // 4
console.log(container.lastElementChild.id); // added

// --- createTextNode + appendChild ---
var textNode = document.createTextNode("hello world");
newDiv.appendChild(textNode);
console.log(newDiv.textContent); // hello world
console.log(newDiv.hasChildNodes()); // true

// --- text node properties ---
console.log(textNode.nodeType); // 3
console.log(textNode.nodeName); // #text
console.log(textNode.nodeValue); // hello world

// --- element nodeName ---
console.log(newDiv.nodeName); // DIV

// --- hasChildNodes ---
var emptyEl = document.createElement("span");
console.log(emptyEl.hasChildNodes()); // false

// --- removeChild ---
var target = document.getElementById("target");
console.log(target.childElementCount); // 3
var spanB = target.children[1];
console.log(spanB.textContent); // B
target.removeChild(spanB);
console.log(target.childElementCount); // 2
console.log(target.children[0].textContent); // A
console.log(target.children[1].textContent); // C

// --- insertBefore ---
var newSpan = document.createElement("span");
var t2 = document.createTextNode("X");
newSpan.appendChild(t2);
var spanC = target.children[1]; // C is now at index 1
target.insertBefore(newSpan, spanC);
console.log(target.childElementCount); // 3
console.log(target.children[0].textContent); // A
console.log(target.children[1].textContent); // X
console.log(target.children[2].textContent); // C

// --- insertBefore with null ref (appends) ---
var endSpan = document.createElement("span");
var t3 = document.createTextNode("END");
endSpan.appendChild(t3);
target.insertBefore(endSpan, null);
console.log(target.lastElementChild.textContent); // END
console.log(target.childElementCount); // 4

// --- textContent setter (replaces all children) ---
var container2 = document.getElementById("container");
var first = document.getElementById("first");
console.log(first.textContent); // First paragraph
first.textContent = "Replaced text";
console.log(first.textContent); // Replaced text

// --- cloneNode(false) - shallow ---
var cloneSrc = document.getElementById("clone-src");
console.log(cloneSrc.childElementCount); // 2
var shallow = cloneSrc.cloneNode(false);
console.log(shallow.tagName); // DIV
console.log(shallow.hasChildNodes()); // false

// --- cloneNode(true) - deep ---
var deep = cloneSrc.cloneNode(true);
console.log(deep.tagName); // DIV
console.log(deep.childElementCount); // 2
console.log(deep.firstElementChild.tagName); // EM
console.log(deep.lastElementChild.tagName); // STRONG

// --- setAttribute / getAttribute / removeAttribute on created element ---
var el = document.createElement("div");
el.setAttribute("data-x", "42");
console.log(el.getAttribute("data-x")); // 42
console.log(el.hasAttribute("data-x")); // true
el.removeAttribute("data-x");
console.log(el.hasAttribute("data-x")); // false

// --- removeAttribute on existing element ---
var main = document.getElementById("container");
main.setAttribute("data-tmp", "yes");
console.log(main.hasAttribute("data-tmp")); // true
main.removeAttribute("data-tmp");
console.log(main.hasAttribute("data-tmp")); // false

// --- innerHTML getter ---
var cloneSrc2 = document.getElementById("clone-src");
var html = cloneSrc2.innerHTML;
console.log(typeof html); // string

// --- normalize (merge adjacent text nodes) ---
var norm = document.createElement("div");
norm.appendChild(document.createTextNode("hello"));
norm.appendChild(document.createTextNode(" world"));
console.log(norm.childNodes.length); // 2
norm.normalize();
console.log(norm.childNodes.length); // 1
console.log(norm.textContent); // hello world

"DOM mutation tests complete";
