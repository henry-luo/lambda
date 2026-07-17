// CSSStyleDeclaration uses DOMString coercion for numeric animation values.
function setDynamic(target, key, value) {
    target[key] = value;
}

var box = document.getElementById('box');
var style = box.style;
style.opacity = 0.25;
console.log('direct:' + style.opacity + ':' + getComputedStyle(box).opacity);
setDynamic(style, 'opacity', 0.5);
console.log('dynamic:' + style.opacity + ':' + getComputedStyle(box).opacity);
