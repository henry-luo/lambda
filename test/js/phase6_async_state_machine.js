// Phase 6: Async/Await Full State Machine Tests
// Tests pending promise suspension and resumption
var assert_count = 0;
function assert(condition, message) {
    assert_count++;
    if (!condition) {
        console.log("FAIL: " + message);
    } else {
        console.log("PASS: " + message);
    }
}

// --- Test 1: Pending promise resolved later ---
var r1 = Promise.withResolvers();
async function waitForPending() {
    var x = await r1.promise;
    return x + 1;
}
var p1 = waitForPending();
r1.resolve(41);

// --- Test 2: Multiple awaits with mix of resolved and pending ---
var r2 = Promise.withResolvers();
async function mixedAwaits() {
    var a = await Promise.resolve(10);  // fast path
    var b = await r2.promise;            // suspends
    var c = await 5;                     // fast path after resume
    return a + b + c;
}
var p2 = mixedAwaits();
r2.resolve(20);

// --- Test 3: Chain of pending promises ---
var r3a = Promise.withResolvers();
var r3b = Promise.withResolvers();
async function chainPending() {
    var a = await r3a.promise;
    var b = await r3b.promise;
    return a * b;
}
var p3 = chainPending();
r3a.resolve(6);
r3b.resolve(7);

// --- Test 4: Rejection through pending promise ---
var r4 = Promise.withResolvers();
async function rejectPending() {
    try {
        var x = await r4.promise;
        return "should not reach";
    } catch (e) {
        return "caught: " + e;
    }
}
var p4 = rejectPending();
r4.reject("pending error");

// --- Test 5: Async function returning pending promise ---
var r5 = Promise.withResolvers();
async function returnPending() {
    return await r5.promise;
}
var p5 = returnPending();
r5.resolve(999);

// --- Test 6: Nested async calls with pending ---
var r6 = Promise.withResolvers();
async function innerPending() {
    return await r6.promise;
}
async function outerPending() {
    var result = await innerPending();
    return result + 1;
}
var p6 = outerPending();
r6.resolve(100);

// --- Test 7: Resolve before await (already resolved, fast path in state machine) ---
var r7 = Promise.withResolvers();
r7.resolve(50);
async function alreadyResolvedFn() {
    return await r7.promise;
}
var p7 = alreadyResolvedFn();

// --- Test 8: Sequential pending awaits in same function ---
var r8a = Promise.withResolvers();
var r8b = Promise.withResolvers();
var r8c = Promise.withResolvers();
async function seqPendingFn() {
    var a = await r8a.promise;
    var b = await r8b.promise;
    var c = await r8c.promise;
    return a + b + c;
}
var p8 = seqPendingFn();
r8a.resolve(1);
r8b.resolve(2);
r8c.resolve(3);

// --- Test 9: Pending promise with computation after resume ---
var r9 = Promise.withResolvers();
async function computeAfterResume() {
    var base = await r9.promise;
    var result = base * 2 + 3;
    return result;
}
var p9 = computeAfterResume();
r9.resolve(10);

// --- Test 10: Async arrow function with pending promise ---
var r10 = Promise.withResolvers();
var asyncArrowPending = async () => {
    var x = await r10.promise;
    return x + 100;
};
var p10 = asyncArrowPending();
r10.resolve(5);

// --- Verify all results ---
async function runTests() {
    var v1 = await p1;
    assert(v1 === 42, "pending resolved: 41+1=42, got: " + v1);

    var v2 = await p2;
    assert(v2 === 35, "mixed awaits: 10+20+5=35, got: " + v2);

    var v3 = await p3;
    assert(v3 === 42, "chain pending: 6*7=42, got: " + v3);

    var v4 = await p4;
    assert(v4 === "caught: pending error", "reject pending: caught, got: " + v4);

    var v5 = await p5;
    assert(v5 === 999, "return pending: 999, got: " + v5);

    var v6 = await p6;
    assert(v6 === 101, "nested pending: 100+1=101, got: " + v6);

    var v7 = await p7;
    assert(v7 === 50, "already resolved: 50, got: " + v7);

    var v8 = await p8;
    assert(v8 === 6, "sequential pending: 1+2+3=6, got: " + v8);

    var v9 = await p9;
    assert(v9 === 23, "compute after resume: 10*2+3=23, got: " + v9);

    var v10 = await p10;
    assert(v10 === 105, "async arrow pending: 5+100=105, got: " + v10);

    console.log("Phase 6 async state machine tests: " + assert_count + " assertions");
}

runTests();
