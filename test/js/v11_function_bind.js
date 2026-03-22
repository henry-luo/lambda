// v11 B3: Function.prototype.bind

// Test 1: basic bind with this
var obj = { x: 10 };
function getX() { return this.x; }
var boundGetX = getX.bind(obj);
console.log(boundGetX());

// Test 2: bind with partial application
function add(a, b) { return a + b; }
var add5 = add.bind(null, 5);
console.log(add5(3));

// Test 3: bind with method extraction
var counter = {
    count: 0,
    increment: function() {
        this.count = this.count + 1;
        return this.count;
    }
};
var inc = counter.increment.bind(counter);
console.log(inc());
console.log(inc());

// Test 4: bind preserves closure environment
function makeMultiplier(factor) {
    return function(x) { return x * factor; };
}
var double_fn = makeMultiplier(2);
var boundDouble = double_fn.bind(null);
console.log(boundDouble(7));

// Test 5: bind with multiple partial args
function sum3(a, b, c) { return a + b + c; }
var sum3_bound = sum3.bind(null, 1, 2);
console.log(sum3_bound(3));

var result = {
    basic_bind: boundGetX(),
    partial_app: add5(3),
    method_extract: counter.count,
    closure_bind: boundDouble(7),
    multi_partial: sum3_bound(3)
};
