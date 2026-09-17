var button = document.getElementById('button');
var div = document.getElementById('div');
var link = document.getElementById('link');
var fragment = document.createDocumentFragment();

console.log(Object.prototype.toString.call(button));
console.log(Object.prototype.toString.call(div));
console.log(Object.prototype.toString.call(fragment));
console.log(button instanceof Element);
console.log(button instanceof HTMLElement);
console.log(button instanceof HTMLButtonElement);
console.log(Object.prototype.toString.call(link));
console.log(link instanceof HTMLElement);
console.log(link instanceof HTMLLinkElement);
