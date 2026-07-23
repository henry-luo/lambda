const originalPush = Array.prototype.push;
const originalIterator = Array.prototype[Symbol.iterator];

function resultOf(label, action) {
    const array = [1];
    action(array);
    console.log(label + ":" + array.join(","));
}

resultOf("assignment", () => {
    Array.prototype.push = function (value) {
        this[0] = value + 10;
        return 91;
    };
    const array = [1];
    console.log("assignment-return:" + array.push(2));
    console.log("assignment-value:" + array.join(","));
    Array.prototype.push = originalPush;
});

Object.defineProperty(Array.prototype, "push", {
    value: function (value) {
        this[0] = value + 20;
        return 92;
    },
    writable: true,
    configurable: true
});
let array = [1];
console.log("define:" + array.push(2) + ":" + array.join(","));
Object.defineProperty(Array.prototype, "push", {
    value: originalPush,
    writable: true,
    configurable: true
});

Reflect.defineProperty(Array.prototype, "push", {
    value: function (value) {
        this[0] = value + 30;
        return 93;
    },
    writable: true,
    configurable: true
});
array = [1];
console.log("reflect-define:" + array.push(2) + ":" + array.join(","));
Reflect.defineProperty(Array.prototype, "push", {
    value: originalPush,
    writable: true,
    configurable: true
});

const proxy = new Proxy(Array.prototype, {});
Reflect.set(proxy, "push", function (value) {
    this[0] = value + 40;
    return 94;
});
array = [1];
console.log("proxy-set:" + array.push(2) + ":" + array.join(","));
Reflect.set(proxy, "push", originalPush);

delete Array.prototype.push;
array = [1];
console.log("delete:" + typeof array.push);
Object.defineProperty(Array.prototype, "push", {
    value: originalPush,
    writable: true,
    configurable: true
});

Array.prototype[Symbol.iterator] = function () {
    let done = false;
    return {
        next() {
            if (done) return {done: true};
            done = true;
            return {value: 77, done: false};
        }
    };
};
console.log("iterator:" + [...[1, 2]].join(","));
Array.prototype[Symbol.iterator] = originalIterator;

const originalMap = Array.prototype.map;
Object.defineProperty(Array.prototype, "map", {
    value: function () { return ["custom"]; },
    writable: true,
    configurable: true
});
console.log("generic:" + [1, 2].map(x => x + 1).join(","));
Object.defineProperty(Array.prototype, "map", {
    value: originalMap,
    writable: true,
    configurable: true
});

const originalPrototype = Array.prototype;
const replacement = Object.create(originalPrototype);
replacement.push = function () { return 95; };
Array.prototype = replacement;
console.log("prototype-replace-rejected:" +
    (Array.prototype === originalPrototype) + ":" + new Array().push(1));
Array.prototype = originalPrototype;

console.log("restored:" + [1].push(2) + ":" + [...[3, 4]].join(","));

const sparse = [];
sparse.length = 999999;
sparse.push("tail");
console.log("sparse-push:" + sparse.length + ":" + sparse[999999]);
