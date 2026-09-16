// T12-3: a direct lexical declaration dominates later branch writes in the
// native body, while a before-declaration assignment remains a checked miss.
function initializedBranch(n) {
    let value = n;
    if (n > 0) {
        value = value + 1;
    } else {
        value = value - 1;
    }
    return value;
}

function beforeDeclarationAssignment() {
    try {
        future = 1;
    } catch (error) {
        return error.name;
    }
    let future = 0;
    return "missed";
}

if (initializedBranch(4) !== 5 || initializedBranch(-4) !== -5 ||
        beforeDeclarationAssignment() !== "ReferenceError") {
    throw new Error("T12 TDZ/native-frame behavior changed");
}
console.log("tune12-tdz-native-frame-ok");
