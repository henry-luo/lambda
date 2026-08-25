// Class heritage expressions run in strict mode, including nested functions.
var Derived = class extends function() { arguments.callee; } {};
var Base = Object.getPrototypeOf(Derived);

try {
    Base.arguments;
    console.log("no error");
} catch (error) {
    console.log(error instanceof TypeError);
}

try {
    new Derived;
    console.log("no error");
} catch (error) {
    console.log(error instanceof TypeError);
}
