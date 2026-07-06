// DOM wrapper identity tests for Radiant-backed host nodes.

var main = document.getElementById("main");
console.log(main === document.querySelector("#main"));

var list = document.getElementById("list");
var firstA = list.firstElementChild;
var firstB = list.firstElementChild;
console.log(firstA === firstB);
console.log(firstA.parentNode === list);

var second = document.getElementById("second");
console.log(second.previousElementSibling === firstA);

var intro = document.getElementById("intro");
var textA = intro.firstChild;
var textB = intro.firstChild;
console.log(textA === textB);
console.log(textA.parentNode === intro);
