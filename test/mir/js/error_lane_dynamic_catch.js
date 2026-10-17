// D8.4.3v2: generic property admission and catch conversion retain independent
// fallible boundaries; the formal branch/tag budget is pinned by the sidecar and ratchet.
function dynamicCaught() {
    try {
        null.missing;
        return 0;
    } catch (error) {
        return 7;
    }
}

if (dynamicCaught() !== 7) throw new Error("dynamic throw was not caught");
