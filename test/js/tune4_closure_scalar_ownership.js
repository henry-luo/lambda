function makeCapturedScalar(seed) {
    var captured = seed + 0.25;
    return function(step) {
        gc();
        return captured + step;
    };
}
var capturedScalar = makeCapturedScalar(7);
gc();
console.log("captured-scalar-env:" + capturedScalar(0.75));

var capturedDeclaration;
{
    let blockScalar = 12.5;
    function declaration(step) {
        gc();
        return blockScalar + step;
    }
    capturedDeclaration = declaration;
}
gc();
console.log("captured-declaration-env:" + capturedDeclaration(1.5));

function escapedScalarHome() {
    let retained = ({ value: 3.25 }).value;
    function readRetained() { return retained; }
    let replacement = ({ value: 9.5 }).value;
    let replacementAgain = ({ value: 12.75 }).value;
    return readRetained() + ":" + replacement + ":" + replacementAgain;
}
console.log("escaped-scalar-home:" + escapedScalarHome());
