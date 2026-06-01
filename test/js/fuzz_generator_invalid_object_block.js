function* fuzzGen() {
    yield 1;

    function safeDivide(a, b) {
        try {
            if (b === 0) {
                throw new Error("Division by zero")
            }
            return a / b
        } catch (error) {
            return "Error: " + error.message
        }
    }

    var test1 = safeDivide(10, 2);
    var test2 = safeDivide(10, 0);

    {
        safeDivideSuccess: test1,
        safeDivideError: test2
    };

    yield 2;
}

var g = fuzzGen();
try {
    while (true) {
        var r = g.next();
        if (r.done) break;
    }
} catch (e) {}
