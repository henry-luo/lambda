// D8.4.3: a genuinely fallible property access needs exactly one tag test;
// entry bookkeeping and catch cleanup must not add another one.
function dynamicCaught() {
    try {
        null.missing;
        return 0;
    } catch (error) {
        return 7;
    }
}

if (dynamicCaught() !== 7) throw new Error("dynamic throw was not caught");
