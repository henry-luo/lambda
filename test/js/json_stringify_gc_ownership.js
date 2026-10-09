function collect() { if (typeof gc === 'function') gc(); }

console.log(JSON.stringify({value: [1], done: false}));
console.log(JSON.stringify({a: [1], b: 'text', c: 42}));

const callbacks = {
    first: {
        toJSON() {
            collect();
            return {nested: [7], last: 'kept'};
        }
    },
    second: 'kept'
};
console.log(JSON.stringify(callbacks, function(key, value) {
    collect();
    return value;
}));

const getter = {
    get nested() { collect(); return {allocated: [2], tail: 3}; },
    tail: 4
};
console.log(JSON.stringify(getter));
console.log(JSON.stringify({nested: [1], tail: 2}, ['nested', 'tail']));
