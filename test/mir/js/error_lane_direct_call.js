// D8.4.3: a direct JS call is MAY_SET and therefore needs one tag test in
// the caller, whereas the callee's known source throw needs none.
function thrower() {
    throw 9;
}

function caughtCall() {
    try {
        thrower();
        return 0;
    } catch (error) {
        return error;
    }
}

if (caughtCall() !== 9) throw new Error("direct call was not caught");
