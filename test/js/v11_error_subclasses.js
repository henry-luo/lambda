// v11: Error subclasses

// TypeError
try {
    throw new TypeError("bad type");
} catch (e) {
    console.log("TypeError name:", e.name);
    console.log("TypeError msg:", e.message);
}

// RangeError
try {
    throw new RangeError("out of range");
} catch (e) {
    console.log("RangeError name:", e.name);
    console.log("RangeError msg:", e.message);
}

// SyntaxError
try {
    throw new SyntaxError("bad syntax");
} catch (e) {
    console.log("SyntaxError name:", e.name);
    console.log("SyntaxError msg:", e.message);
}

// ReferenceError
try {
    throw new ReferenceError("not defined");
} catch (e) {
    console.log("ReferenceError name:", e.name);
    console.log("ReferenceError msg:", e.message);
}

// Basic Error still works
try {
    throw new Error("basic error");
} catch (e) {
    console.log("Error name:", e.name);
    console.log("Error msg:", e.message);
}
