// T12-2: generic calls keep coercion while guarded Number calls use the
// function's native numeric body.
function accumulated(n) {
    let total = 0;
    let cursor = n;
    while (cursor > 0) {
        total = total + cursor;
        cursor = cursor - 1;
    }
    return total;
}

function fibonacci(n) {
    if (n < 2) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

function local_alias(n) {
    const alias = n;
    return alias + 0;
}

function joined_local(n) {
    let result = 0;
    if (n > 0) {
        result = n;
    } else {
        result = n + 1;
    }
    return result;
}

function even_component(n) {
    if (n === 0) return 0;
    return odd_component(n - 1) + 1;
}

function odd_component(n) {
    if (n === 0) return 1;
    return even_component(n - 1) + 1;
}

function mixed_return(n) {
    if (n > 0) return n;
    return "fallback";
}

function rewritten_parameter(n) {
    n = "rewritten";
    return n;
}

function rebound_recursive(n) {
    if (n === 0) return 0;
    return rebound_recursive(n - 1) + 1;
}

function tdz_assignment_is_preserved() {
    try {
        future_value = 1;
    } catch (error) {
        return error.name;
    }
    let future_value = 0;
    return "missed";
}

console.log("numbers:" + accumulated(10) + "," + fibonacci(10));
console.log("generic:" + accumulated("3") + "," + fibonacci("7"));
console.log("locals:" + local_alias(4) + "," + joined_local(3) + "," +
    joined_local(-2) + "," + even_component(4));
console.log("mixed:" + mixed_return(2) + "," + mixed_return(0) + "," +
    rewritten_parameter(1));
console.log("numbers-edge:" + accumulated() + "," + accumulated(-0) + "," +
    accumulated(NaN) + "," + accumulated(2.5));

var extra_effects = 0;
function extra_argument() {
    extra_effects++;
    return 99;
}
console.log("extra:" + accumulated(3, extra_argument()) + "," + extra_effects);

var coercions = 0;
var coercing_number = { valueOf: function() {
    coercions++;
    return 3;
} };
console.log("coercion:" + accumulated(coercing_number) + "," + coercions);

try {
    accumulated(2n);
    console.log("bigint:no-error");
} catch (error) {
    console.log("bigint:" + error.name);
}
try {
    local_alias(Symbol("n"));
    console.log("symbol:no-error");
} catch (error) {
    console.log("symbol:" + error.name);
}

var saved_recursive = rebound_recursive;
rebound_recursive = function(n) { return 40 + n; };
console.log("rebound:" + saved_recursive(2));
console.log("tdz:" + tdz_assignment_is_preserved());
