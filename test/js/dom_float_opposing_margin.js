var first = document.getElementById("first");
var second = document.getElementById("second");
console.log(Math.round(second.getBoundingClientRect().top -
    first.getBoundingClientRect().top));
