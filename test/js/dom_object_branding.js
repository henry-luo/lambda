var button = document.getElementById('button');
var div = document.getElementById('div');
var fragment = document.createDocumentFragment();

console.log(Object.prototype.toString.call(button));
console.log(Object.prototype.toString.call(div));
console.log(Object.prototype.toString.call(fragment));
console.log(button instanceof Element);
console.log(button instanceof HTMLElement);
console.log(button instanceof HTMLButtonElement);
