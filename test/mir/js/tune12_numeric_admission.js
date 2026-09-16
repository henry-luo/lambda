// T12-2: returned local Number facts and recursive components select native
// bodies; mixed returns retain the boxed implementation.
function returnedAccumulator(n) {
    let total = 0;
    let cursor = n;
    while (cursor > 0) {
        total = total + cursor;
        cursor = cursor - 1;
    }
    return total;
}

function recursiveComponentLeft(n) {
    if (n === 0) return 0;
    return recursiveComponentRight(n - 1) + 1;
}

function recursiveComponentRight(n) {
    if (n === 0) return 1;
    return recursiveComponentLeft(n - 1) + 1;
}

function mixedNumberOrString(n) {
    if (n > 0) return n;
    return "fallback";
}

if (returnedAccumulator(10) !== 55 || recursiveComponentLeft(4) !== 4 ||
        mixedNumberOrString(0) !== "fallback" ||
        mixedNumberOrString(2) !== 2) {
    throw new Error("T12 numeric admission changed semantics");
}
console.log("tune12-numeric-admission-ok");
