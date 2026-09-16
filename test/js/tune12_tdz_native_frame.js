// T12-3: lexical initialization is identity- and control-flow-proven; only
// an initialized, uncaptured native local can omit TDZ and Item traffic.
function initialized_branch(n) {
    let value = n;
    if (n > 0) {
        value = value + 1;
    } else {
        value = value - 1;
    }
    return value;
}

function zero_iteration(n) {
    let value = n;
    while (false) {
        value = 0;
    }
    value = value + 1;
    return value;
}

function shadowed_binding(n) {
    let value = n;
    {
        let value = n + 1;
        value = value + 1;
    }
    value = value + 2;
    return value;
}

function captured_binding(n) {
    let value = n;
    var read = function() { return value; };
    value = value + 1;
    return read();
}

function assignment_before_declaration() {
    try {
        future = 1;
    } catch (error) {
        return error.name;
    }
    let future = 0;
    return "missed";
}

function const_write_is_separate() {
    const value = 1;
    try {
        value = 2;
    } catch (error) {
        return error.name;
    }
    return "missed";
}

function try_finally_value(n) {
    let value = n;
    try {
        value = value + 1;
    } finally {
        value = value + 1;
    }
    return value;
}

function default_value(value = 3) {
    return value + 1;
}

console.log("initialized:" + initialized_branch(4) + "," + initialized_branch(-4));
console.log("loop:" + zero_iteration(4));
console.log("shadow:" + shadowed_binding(4));
console.log("capture:" + captured_binding(4));
console.log("tdz:" + assignment_before_declaration());
console.log("const:" + const_write_is_separate());
console.log("finally:" + try_finally_value(3));
console.log("default:" + default_value());
