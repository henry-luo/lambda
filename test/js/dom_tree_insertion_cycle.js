// replacing a linked sibling must preserve both DOM order and the backing child list.
var root = document.createElement('div');
var a = document.createElement('span');
var b = document.createElement('span');
var c = document.createElement('span');
var d = document.createElement('span');
a.id = 'a'; b.id = 'b'; c.id = 'c'; d.id = 'd';
root.appendChild(a);
root.appendChild(b);
root.appendChild(c);
root.appendChild(d);
console.log(root.replaceChild(c, b) === b,
            Array.from(root.childNodes).map(n => n.id).join(','));
console.log(root.replaceChild(a, d) === d,
            Array.from(root.childNodes).map(n => n.id).join(','));

// ancestor insertion must throw without changing the existing links.
for (var op of [
    () => a.appendChild(a),
    () => a.insertBefore(root, null),
    () => root.replaceChild(root, a),
    () => a.append(root),
    () => a.prepend(root)
]) {
    try {
        op();
        console.log('missing-error');
    } catch (err) {
        console.log(err.name);
    }
}
console.log(Array.from(root.childNodes).map(n => n.id).join(','));
