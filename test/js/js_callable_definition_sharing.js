'use strict';

function factory(value) {
    return function update(step = 1) { value += step; return value; };
}
var closures = [];
for (var i = 0; i < 256; i++) closures.push(factory(i));
closures[0].label = 'first';
closures[1].prototype = { independent: true };
console.log('captures', closures[0](), closures[1](3), closures[255](2));
console.log('identity', closures[0] !== closures[1], closures[1].label === undefined,
    closures[0].prototype !== closures[1].prototype);
console.log('metadata', closures[0].name, closures[0].length,
    closures[0].toString() === closures[1].toString());
Object.defineProperty(closures[0], 'length', { value: 17 });
Object.defineProperty(closures[0], 'name', { value: 'renamed' });
console.log('metadata-mutation', closures[0].length, closures[1].length,
    closures[0].name, closures[1].name);
function objectFactory(value) { return { value: value, get() { return this.value; } }; }
var a = objectFactory(7), b = objectFactory(9);
a.get.label = 'method';
console.log('methods', a.get(), b.get(), a.get !== b.get, b.get.label === undefined);
function classFactory(value) { return class { field = value; read = () => value; }; }
var A = classFactory(11), B = classFactory(13);
var av = new A(), bv = new B();
console.log('field-definitions', av.field, bv.field, av.read(), bv.read(), av.read !== bv.read);
var distinct = [];
for (var j = 0; j < 32; j++) distinct.push(new Function('x', 'return x + ' + j));
console.log('dynamic-definitions', distinct[0](2), distinct[31](2), distinct[0] !== distinct[1]);
function* resumable(value) { var next = factory(value); yield next(); return next(2); }
var generator = resumable(10);
console.log('suspension', generator.next().value, generator.next().value);
console.log('retained', closures[0](2), closures[1](2));
