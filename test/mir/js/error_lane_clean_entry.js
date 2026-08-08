// D8.4.3: a new activation and an infallible try prologue are CLEAN.  This
// catches regressions that mistake js_with_save_depth's scalar result for an
// ERROR Item.
function cleanTry() {
    try {
        return 1;
    } catch (error) {
        return 2;
    }
}

if (cleanTry() !== 1) throw new Error("clean try returned the wrong value");
