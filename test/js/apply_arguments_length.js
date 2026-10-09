function check(actual, expected, label) {
    if (actual !== expected) throw new Error(label + ': ' + actual);
}
function collect() {
    return Array.prototype.join.call(arguments, '|');
}
function Box() {
    this.values = collect.apply(null, arguments);
}

function grow() {
    Array.prototype.unshift.call(arguments, 'base');
    check(collect.apply(null, arguments), 'base|element|options', 'grown apply');
    check(Reflect.apply(collect, null, arguments), 'base|element|options', 'grown reflect');
    check(Reflect.construct(Box, arguments).values, 'base|element|options', 'grown construct');
    arguments[Symbol.toStringTag] = 'Changed';
    check(collect.apply(null, arguments), 'base|element|options', 'mutable arguments tag');
}
grow('element', 'options');
console.log('apply-grown-arguments-ok');

function shrink() {
    arguments.length = 1;
    check(collect.apply(null, arguments), 'first', 'shortened length');
    arguments.length = '2.9';
    check(collect.apply(null, arguments), 'first|second', 'string length');
    arguments.length = -1;
    check(collect.apply(null, arguments), '', 'negative length');
    arguments.length = undefined;
    check(collect.apply(null, arguments), '', 'undefined length');
    delete arguments.length;
    check(collect.apply(null, arguments), '', 'deleted length');
}
shrink('first', 'second', 'third');
console.log('apply-arguments-length-coercion-ok');

function observable() {
    const order = [];
    Object.defineProperty(arguments, 'length', {get: function () {
        order.push('length');
        return {valueOf: function () {order.push('convert'); return 3;}};
    }});
    Object.defineProperty(arguments, '2', {get: function () {
        order.push('index'); return 'third';
    }});
    check(collect.apply(null, arguments), 'first|second|third', 'observable getters');
    check(order.join('|'), 'length|convert|index', 'getter order');
}
observable('first', 'second');
console.log('apply-arguments-getters-ok');

const marker = {};
const throwing = {get length() {throw marker;}};
for (const invoke of [
    function () {collect.apply(null, throwing);},
    function () {Reflect.apply(collect, null, throwing);},
    function () {Reflect.construct(Box, throwing);}
]) {
    let caught = false;
    try {invoke();} catch (error) {caught = error === marker;}
    check(caught, true, 'length abrupt completion');
}
check(collect.apply(null, null), '', 'apply null');
check(collect.apply(null, undefined), '', 'apply undefined');
for (const invoke of [
    function () {Reflect.apply(collect, null, null);},
    function () {Reflect.construct(Box, undefined);}
]) {
    let caught = false;
    try {invoke();} catch (error) {caught = error instanceof TypeError;}
    check(caught, true, 'reflect non-object arguments');
}
check(collect.apply(null, [1, 2, 3]), '1|2|3', 'numeric array');
check(collect.apply(null, {0: 'first', 1: 'second', length: '2.9'}),
    'first|second', 'array-like string length');
console.log('apply-arguments-completions-ok');
