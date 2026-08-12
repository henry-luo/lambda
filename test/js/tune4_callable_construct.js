function Base(a, b) {
    this.args = a + ":" + b;
    this.seenTarget = new.target;
}

var Bound = Base.bind({ ignored: true }, "left");
var Nested = Bound.bind(null);
var boundValue = new Nested("right");
console.log(boundValue.args);
console.log(boundValue.seenTarget === Base);
console.log(boundValue instanceof Base);

function Alternate() {}
Alternate.prototype.kind = "alternate";
var reflected = Reflect.construct(Base, ["r", "c"], Alternate);
console.log(reflected.args);
console.log(reflected.seenTarget === Alternate);
console.log(Object.getPrototypeOf(reflected) === Alternate.prototype);

Base.name = "RenamedWithoutSemanticEffect";
var renamed = new Base("n", "m");
console.log(renamed.args);
console.log(renamed instanceof Base);

var trapTarget = null;
var proxy = new Proxy(Base, {
    construct: function (target, args, newTarget) {
        trapTarget = newTarget;
        return Reflect.construct(target, args, newTarget);
    }
});
var proxied = new proxy("p", "x");
console.log(proxied.args);
console.log(trapTarget === proxy);

var arrow = () => 1;
try {
    new arrow();
} catch (error) {
    console.log(error instanceof TypeError);
}

function ordinary() { return new.target; }
console.log(ordinary() === undefined);
try {
    Reflect.construct(function () { throw new Error("stop"); }, []);
} catch (error) {
    console.log(error.message);
}
console.log(ordinary() === undefined);
