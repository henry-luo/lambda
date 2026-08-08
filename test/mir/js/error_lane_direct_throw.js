// D8.4.3: a source throw already owns an ERROR Item, so catch routing must
// jump directly without materializing a redundant tag test.
function directCaught() {
    try {
        throw 7;
    } catch (error) {
        return error;
    }
}

if (directCaught() !== 7) throw new Error("direct throw was not caught");
