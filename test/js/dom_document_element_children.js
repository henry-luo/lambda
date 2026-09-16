console.log(document.firstElementChild === document.documentElement);
console.log(document.lastElementChild === document.documentElement);
console.log(document.childElementCount);

var script = document.createElement('script');
document.lastElementChild.appendChild(script);
console.log(script.parentNode === document.documentElement);
