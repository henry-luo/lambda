// HTMLElement.dir reflects the global `dir` content attribute and defaults to
// the empty DOMString when the attribute is absent.
var subject = document.getElementById('subject');

console.log(JSON.stringify(subject.dir));
subject.setAttribute('dir', 'rtl');
console.log(JSON.stringify(subject.dir));

subject.dir = 'ltr';
console.log(JSON.stringify(subject.getAttribute('dir')));

var detached = document.createElement('section');
console.log(JSON.stringify(detached.dir));
detached.dir = 'auto';
console.log(JSON.stringify(detached.getAttribute('dir')));
