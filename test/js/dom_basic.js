// DOM Basic Tests - document property access, getElementById, querySelector, etc.

// Test document.documentElement
var docEl = document.documentElement;
console.log(docEl.tagName);

// Test document.body
var body = document.body;
console.log(body.tagName);

// Test document.title
console.log(document.title);

// Test document.getElementById
var main = document.getElementById("main");
console.log(main.id);
console.log(main.className);
console.log(main.tagName);

// Test element that doesn't exist
var noexist = document.getElementById("nonexistent");
console.log(noexist);

// Test document.querySelector
var intro = document.querySelector("#intro");
console.log(intro.textContent);

// Test document.querySelectorAll
var items = document.querySelectorAll(".item");
console.log(items.length);

// Test childElementCount
var list = document.getElementById("list");
console.log(list.childElementCount);

// Test firstElementChild / lastElementChild
var first = list.firstElementChild;
console.log(first.textContent);

var last = list.lastElementChild;
console.log(last.textContent);

// Test getAttribute
console.log(main.getAttribute("class"));

// Test hasAttribute
console.log(main.hasAttribute("id"));
console.log(main.hasAttribute("data-x"));

// Test children array
var children = list.children;
console.log(children.length);

// Test nextElementSibling / previousElementSibling
var second = first.nextElementSibling;
console.log(second.textContent);

var backToFirst = second.previousElementSibling;
console.log(backToFirst.textContent);

// Test parentElement
var parent = first.parentElement;
console.log(parent.tagName);

// Test nodeType (1 = ELEMENT_NODE)
console.log(main.nodeType);

// Test querySelector on element
var special = list.querySelector(".special");
console.log(special.textContent);

// Test matches
console.log(special.matches(".item"));
console.log(special.matches(".special"));

// Test closest
var closestContainer = special.closest(".container");
console.log(closestContainer.id);

// Test getElementsByClassName
var containers = document.getElementsByClassName("container");
console.log(containers.length);

// Test getElementsByTagName
var lis = document.getElementsByTagName("li");
console.log(lis.length);

// Final value
"DOM tests complete";
