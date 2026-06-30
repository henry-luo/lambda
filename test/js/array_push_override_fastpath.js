function assertEq(actual, expected, label) {
    if (actual !== expected) {
        throw new Error(label + ": expected " + expected + ", got " + actual);
    }
}

var a = [];
assertEq(a.push(1), 1, "default push return");
assertEq(a.length, 1, "default push length");
assertEq(a[0], 1, "default push value");

var ownCalls = 0;
var b = [];
b.push = function(value) {
    ownCalls = ownCalls + 1;
    this[this.length] = value + 10;
    return 77;
};
assertEq(b.push(5), 77, "own push return");
assertEq(ownCalls, 1, "own push call count");
assertEq(b.length, 1, "own push length");
assertEq(b[0], 15, "own push value");

var originalPush = Array.prototype.push;
var protoCalls = 0;
Array.prototype.push = function(value) {
    protoCalls = protoCalls + 1;
    this[this.length] = value + 20;
    return 88;
};
var c = [];
assertEq(c.push(6), 88, "prototype push return");
assertEq(protoCalls, 1, "prototype push call count");
assertEq(c.length, 1, "prototype push length");
assertEq(c[0], 26, "prototype push value");

Array.prototype.push = originalPush;
var d = [];
assertEq(d.push(7), 1, "restored push return");
assertEq(d[0], 7, "restored push value");

var setterCalls = 0;
Object.defineProperty(Array.prototype, "0", {
    configurable: true,
    set: function(value) {
        setterCalls = setterCalls + value;
    }
});
var e = [];
assertEq(e.push(3), 1, "numeric setter push return");
assertEq(setterCalls, 3, "numeric setter observed");
assertEq(e.length, 1, "numeric setter length");
delete Array.prototype[0];
