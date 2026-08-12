Object.defineProperty(Array, "name", { value: "RenamedArray" });
var array = new Array(1, 2);
console.log(array.length + ":" + array[0]);

Object.defineProperty(Date, "name", { value: "RenamedDate" });
console.log(typeof Date());
console.log(new Date(0) instanceof Date);

Object.defineProperty(Function, "name", { value: "RenamedFunction" });
console.log(Function("return 6")());

try { new Symbol("x"); } catch (error) { console.log(error.name); }
try { Map(); } catch (error) { console.log(error.name); }

function Inner() {
    console.log("inner:" + (new.target === Inner));
    throw new Error("boom");
}
function Outer() {
    try { Reflect.construct(Inner, [], Inner); }
    catch (error) { console.log("caught"); }
    console.log("outer:" + (new.target === Outer));
}
new Outer();

function NewTarget() {}
NewTarget.prototype = { marker: 13 };
var reflected = Reflect.construct(Array, [2], NewTarget);
console.log(Object.getPrototypeOf(reflected).marker + ":" + reflected.length);

var BoundArray = Array.bind(null, 1);
var bound = Reflect.construct(BoundArray, [2], NewTarget);
console.log(Object.getPrototypeOf(bound).marker + ":" + bound.length +
    ":" + bound[0] + ":" + bound[1]);

var ConstructProxy = new Proxy(function() {}, {
    construct: function(target, args, newTarget) {
        console.log(newTarget === ConstructProxy);
        return { value: args[0] };
    }
});
console.log(new ConstructProxy(8).value);

var GeneratorFunction = Object.getPrototypeOf(function*() {}).constructor;
Object.defineProperty(GeneratorFunction, "name", { value: "RenamedGenerator" });
console.log(GeneratorFunction("yield 9")().next().value);
