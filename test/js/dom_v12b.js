// DOM v12b Tests - createDocumentFragment, createComment, importNode, adoptNode,
// innerHTML setter, replaceChild, insertAdjacentHTML, insertAdjacentElement,
// remove, toggleAttribute, style.setProperty, style.removeProperty

// === createDocumentFragment ===
var frag = document.createDocumentFragment();
console.log(frag.tagName); // #DOCUMENT-FRAGMENT
var a = document.createElement("span");
a.textContent = "A";
var b = document.createElement("span");
b.textContent = "B";
frag.appendChild(a);
frag.appendChild(b);
console.log(frag.childElementCount); // 2

// appendChild with fragment moves children
var main = document.getElementById("main");
main.appendChild(frag);
console.log(main.childElementCount); // 5 (3 + 2 from fragment)
console.log(frag.childElementCount); // 0 (children moved out)
console.log(main.lastElementChild.textContent); // B

// insertBefore with fragment
var frag2 = document.createDocumentFragment();
var c = document.createElement("span");
c.textContent = "C";
frag2.appendChild(c);
main.insertBefore(frag2, main.firstElementChild);
console.log(main.firstElementChild.textContent); // C
console.log(main.childElementCount); // 6

// === createComment ===
var comment = document.createComment("test comment");
console.log(comment.nodeType); // 8
console.log(comment.data); // test comment
console.log(comment.nodeName); // #comment

// === importNode (deep clone) ===
var src = document.getElementById("replace-target");
var imported = document.importNode(src, true);
console.log(imported.tagName); // DIV
console.log(imported.firstElementChild.textContent); // Old

// === adoptNode (detach from parent) ===
var para2 = document.getElementById("para2");
var para2Parent = para2.parentElement.tagName; // DIV
var adopted = document.adoptNode(para2);
console.log(para2Parent); // DIV
console.log(adopted.textContent); // Second

// === innerHTML setter ===
var div = document.createElement("div");
div.innerHTML = "<p>Hello</p><span>World</span>";
console.log(div.childElementCount); // 2
console.log(div.firstElementChild.tagName); // P
console.log(div.firstElementChild.textContent); // Hello
console.log(div.lastElementChild.tagName); // SPAN

// innerHTML clear
div.innerHTML = "";
console.log(div.childElementCount); // 0
console.log(div.hasChildNodes()); // false

// innerHTML with plain text (no HTML tags)
div.innerHTML = "just text";
console.log(div.hasChildNodes()); // true
console.log(div.textContent); // just text

// innerHTML with nested elements
div.innerHTML = "<ul><li>one</li><li>two</li></ul>";
console.log(div.firstElementChild.tagName); // UL
console.log(div.firstElementChild.childElementCount); // 2

// === replaceChild ===
var replaceTarget = document.getElementById("replace-target");
var oldChild = document.getElementById("old-child");
var newChild = document.createElement("em");
newChild.textContent = "New";
var removed = replaceTarget.replaceChild(newChild, oldChild);
console.log(removed.textContent); // Old
console.log(replaceTarget.firstElementChild.tagName); // EM
console.log(replaceTarget.firstElementChild.textContent); // New

// === insertAdjacentElement ===
var adjTarget = document.getElementById("adjacent-target");

var before1 = document.createElement("b");
before1.textContent = "Before";
adjTarget.insertAdjacentElement("afterbegin", before1);
console.log(adjTarget.firstElementChild.textContent); // Before

var after1 = document.createElement("i");
after1.textContent = "After";
adjTarget.insertAdjacentElement("beforeend", after1);
console.log(adjTarget.lastElementChild.textContent); // After

console.log(adjTarget.childElementCount); // 3

// === insertAdjacentHTML ===
var div2 = document.createElement("div");
div2.innerHTML = "<span>X</span>";
div2.insertAdjacentHTML("afterbegin", "<b>START</b>");
console.log(div2.firstElementChild.tagName); // B
console.log(div2.firstElementChild.textContent); // START
console.log(div2.childElementCount); // 2

div2.insertAdjacentHTML("beforeend", "<i>END</i>");
console.log(div2.lastElementChild.tagName); // I
console.log(div2.childElementCount); // 3

// === remove ===
var removeTarget = document.getElementById("remove-target");
console.log(removeTarget.childElementCount); // 2
var removable = document.getElementById("removable");
removable.remove();
console.log(removeTarget.childElementCount); // 1
console.log(removeTarget.firstElementChild.textContent); // Stay

// === toggleAttribute ===
var tog = document.getElementById("toggle-target");
console.log(tog.hasAttribute("hidden")); // true

// toggle off
var result = tog.toggleAttribute("hidden");
console.log(result); // false
console.log(tog.hasAttribute("hidden")); // false

// toggle on
result = tog.toggleAttribute("hidden");
console.log(result); // true
console.log(tog.hasAttribute("hidden")); // true

// toggle with force=true (add)
result = tog.toggleAttribute("disabled", true);
console.log(result); // true
console.log(tog.hasAttribute("disabled")); // true

// toggle with force=false (remove)
result = tog.toggleAttribute("disabled", false);
console.log(result); // false
console.log(tog.hasAttribute("disabled")); // false

// === style.setProperty ===
var styEl = document.createElement("div");
styEl.style.setProperty("color", "blue");
console.log(styEl.style.color); // blue

styEl.style.setProperty("margin-top", "10px");
console.log(styEl.style.marginTop); // 10px
console.log(styEl.style.color); // blue (unchanged by margin-top)

// === style.removeProperty (with multiple properties) ===
var oldColor = styEl.style.removeProperty("color");
console.log(oldColor); // blue
console.log(styEl.style.color); // (empty string)
console.log(styEl.style.marginTop); // 10px (unchanged)

"DOM v12b tests complete";
