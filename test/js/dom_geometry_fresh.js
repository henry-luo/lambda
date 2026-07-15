var box = document.createElement('div');
box.style.width = '40px';
box.style.height = '25px';
document.getElementById('host').appendChild(box);

var first = box.getBoundingClientRect();
console.log('initial:', Math.round(first.width), Math.round(first.height));

box.style.width = '135px';
box.style.height = '55px';
var second = box.getBoundingClientRect();
console.log('mutated:', Math.round(second.width), Math.round(second.height));

var third = box.getBoundingClientRect();
console.log('stable:', Math.round(third.width), Math.round(third.height));
console.log('GEOMETRY_FRESH_DONE');
