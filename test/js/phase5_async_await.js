// Phase 5: Async/Await — Synchronous Fast Path Tests
var assert_count = 0;
function assert(condition, message) {
    assert_count++;
    if (!condition) {
        console.log("FAIL: " + message);
    } else {
        console.log("PASS: " + message);
    }
}

// --- Test 1: Basic async function returns a Promise ---
async function basicAsync() {
    return 42;
}
var r1 = basicAsync();
assert(r1 !== undefined && r1 !== null, "async function returns something");
// The result should be a promise-like object wrapping 42

// --- Test 2: Await on a non-promise value ---
async function awaitNonPromise() {
    var x = await 10;
    return x;
}
var r2 = awaitNonPromise();
// r2 should be a resolved promise containing 10

// --- Test 3: Await on a resolved Promise ---
async function awaitResolved() {
    var p = Promise.resolve(99);
    var x = await p;
    return x;
}
var r3 = awaitResolved();

// --- Test 4: Async function with multiple awaits ---
async function multiAwait() {
    var a = await 1;
    var b = await 2;
    var c = await 3;
    return a + b + c;
}
var r4 = multiAwait();

// --- Test 5: Async function with exception becomes rejected promise ---
async function throwingAsync() {
    throw new Error("async error");
}
// Call it - should not crash, should return a rejected promise
var r5 = throwingAsync();
assert(r5 !== undefined && r5 !== null, "throwing async returns something (rejected promise)");

// --- Test 6: Await on rejected promise in try/catch ---
async function awaitRejected() {
    try {
        var x = await Promise.reject("bad");
        return "should not reach";
    } catch (e) {
        return "caught: " + e;
    }
}
var r6 = awaitRejected();

// --- Test 7: Async arrow function ---
var asyncArrow = async () => {
    var x = await 5;
    return x * 2;
};
var r7 = asyncArrow();

// --- Test 8: Async arrow with expression body ---
var asyncArrowExpr = async () => await Promise.resolve(777);
var r8 = asyncArrowExpr();

// --- Test 9: Nested async calls ---
async function inner() {
    return await Promise.resolve(100);
}
async function outer() {
    var x = await inner();
    return x + 1;
}
var r9 = outer();

// Now verify results using Promise.then or direct unwrapping
// Since we're in synchronous fast-path, promises should be immediately resolved

// For sync fast-path, we can use await directly in an async wrapper
async function runTests() {
    var v2 = await awaitNonPromise();
    assert(v2 === 10, "await non-promise value = 10, got: " + v2);

    var v3 = await awaitResolved();
    assert(v3 === 99, "await resolved promise = 99, got: " + v3);

    var v4 = await multiAwait();
    assert(v4 === 6, "multiple awaits sum = 6, got: " + v4);

    var v6 = await awaitRejected();
    assert(v6 === "caught: bad", "await rejected in try/catch = 'caught: bad', got: " + v6);

    var v7 = await asyncArrow();
    assert(v7 === 10, "async arrow = 10, got: " + v7);

    var v8 = await asyncArrowExpr();
    assert(v8 === 777, "async arrow expression = 777, got: " + v8);

    var v9 = await outer();
    assert(v9 === 101, "nested async = 101, got: " + v9);

    // Test: await on throwing async — catch at caller level
    try {
        var v5 = await throwingAsync();
        assert(false, "should not reach after await throwingAsync()");
    } catch (e) {
        assert(e.message === "async error", "await throwing async caught: " + e.message);
    }

    // Test: async function with no explicit return → Promise<undefined>
    async function noReturn() {
        var x = 1 + 2;
    }
    var v10 = await noReturn();
    assert(v10 === undefined, "async no return = undefined, got: " + v10);

    // Test: Promise.all with async functions
    async function addOne(n) { return n + 1; }
    var results = await Promise.all([addOne(1), addOne(2), addOne(3)]);
    assert(results[0] === 2, "Promise.all[0] = 2, got: " + results[0]);
    assert(results[1] === 3, "Promise.all[1] = 3, got: " + results[1]);
    assert(results[2] === 4, "Promise.all[2] = 4, got: " + results[2]);

    console.log("Phase 5 async/await tests: " + assert_count + " assertions");
}

runTests();
