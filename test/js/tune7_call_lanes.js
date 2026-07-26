// Tune7 call-dispatch differential fixture. Run once normally and once with
// JS_CALL_FORCE_GENERIC=1; the observable output must be identical.
function plain(a, b) { return a + b; }
function closureFactory(seed) {
    return function closure(value) { return seed + value; };
}

class Vault {
    #value = 40;
    add(value) { return this.#value + value; }
}

var closure = closureFactory(5);
var vault = new Vault();
console.log(plain(2, 3));
console.log(closure(6));
console.log(vault.add(2));

// Callee and caller with-scopes must stay on the generic path and restore.
var withResult;
with ({ n: 9 }) {
    withResult = (function fromWith() { return n + 1; })();
}
console.log(withResult);

var home = function home() {};
home.__home_class__ = 17;
console.log(home.__home_class__);
Object.defineProperty(home, "__home_class__", {
    value: 19, writable: true, enumerable: true, configurable: true
});
console.log(home.__home_class__);
console.log(delete home.__home_class__);
console.log(home.__home_class__ === undefined);

console.log(plain.bind(null, 7)(8));
