// Structural MIR probe: loops, iteration, and abrupt completion.
function tuneForLoop(limit) {
    let total = 0;
    for (let index = 0; index < limit; index++) total += index;
    return total;
}
function tuneWhileLoop(limit) {
    let index = 0;
    let total = 0;
    while (index < limit) {
        total += index;
        index++;
    }
    return total;
}
function tuneForOf(values) {
    let total = 0;
    for (const value of values) total += value;
    return total;
}
function tuneSwitch(value) {
    switch (value) {
    case 0: return "zero";
    case 1: return "one";
    default: return "other";
    }
}
function tuneTryCatch(object) {
    try { return object.read(); }
    catch (error) { return error.message; }
}
function tuneTryFinally(callback) {
    let value = 1;
    try {
        value = callback();
        return value;
    } finally {
        value = value + 1;
    }
}
function tuneThrow(value) {
    if (value) throw value;
    return 0;
}
const tuneSafeReader = {read() { return 11; }};
tuneForLoop(5);
tuneWhileLoop(5);
tuneForOf([1, 2, 3]);
tuneSwitch(1);
tuneTryCatch(tuneSafeReader);
tuneTryFinally(function() { return 4; });
try { tuneThrow("expected"); } catch (error) { void error; }
