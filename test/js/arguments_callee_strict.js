// Test arguments.callee and strict mode behavior

// t1: non-strict arguments.callee returns the function
function sloppy() {
    return arguments.callee === sloppy;
}
console.log("t1:" + sloppy());

// t2: arguments.length reflects actual args (not formal params)
function testLen(a, b, c) {
    return arguments.length;
}
console.log("t2:" + testLen(1, 2));

// t3: Mapped arguments — arguments[i]=val aliases param
function mapped(a) {
    arguments[0] = 42;
    return a;
}
console.log("t3:" + mapped(1));

// t4: Mapped arguments — param=val aliases arguments[i]
function mapped2(a) {
    a = 99;
    return arguments[0];
}
console.log("t4:" + mapped2(1));

// t5: Strict mode callee throws TypeError
function strictCallee() {
    "use strict";
    try {
        var x = arguments.callee;
        return "FAIL";
    } catch (e) {
        return e instanceof TypeError;
    }
}
console.log("t5:" + strictCallee());

// t6: Strict mode - no aliasing
function strictNoAlias(a) {
    "use strict";
    arguments[0] = 42;
    return a;
}
console.log("t6:" + strictNoAlias(1));

// t7: Symbol.toStringTag
function testTag() {
    return Object.prototype.toString.call(arguments);
}
console.log("t7:" + testTag());

// t8: Non-strict callee in nested function
function outer() {
    function inner() {
        return arguments.callee === inner;
    }
    return inner();
}
console.log("t8:" + outer());
