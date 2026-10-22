// A `var` declared null and assigned in a loop is not statically null on the
// next turn. The JIT lowered `s.map == w` as a null test of `s.map` from the
// declaration's type, so the HLS engine rewrote an fMP4 init map before every
// segment; T0 compared the values.
pn main() {
    let shared = {uri: "init", range: {length: 120, start: 0}}
    let segs = [{map: shared}, {map: shared}, {map: shared}]
    var written = null
    var writes = 0
    for (s in segs) {
        if (s.map != null and s.map != written) {
            writes = writes + 1
            written = s.map
        }
    }
    var w = null
    var seen = []
    for (s in segs) {
        seen = [*seen, s.map == w]
        w = s.map
    }
    // an int-valued var and one that starts as a map took the generic path all along
    var k = null
    var ks = []
    for (x in [5, 5]) {
        ks = [*ks, x == k]
        k = x
    };
    [writes, seen, ks]
}
