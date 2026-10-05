// Text benchmark jq_tree, typed: jq/tree.jq written as a typed Lambda query.
//   def tree($d): nth($d; 0 | recurse([., .]));
//   tree(depth) as $t | reduce range(rounds) as $r (0;
//     ($t | (.. | scalars) |= . + 1 + $r) as $u
//     | (. * 31 + ($u | [paths] | length) + ($u | flatten | add)) % 1000000007)
import ~~.jq_query_typed

let rounds: int = 7
let depth: int = 17

pn main() {
    let t0 = clock()
    let t = jq_tree(depth)
    var checksum: int = 0
    var r: int = 0
    while (r < rounds) {
        let u = jq_bump_scalars(t, 1 + r)
        var paths: array = []
        jq_collect_paths(u, [], false, paths)
        var leaves: int[] = []
        jq_flatten_into(u, leaves)
        checksum = (checksum * 31 + len(paths) + sum(leaves)) % jq_modulus
        r = r + 1
    }
    let t1 = clock()
    if (checksum == 313746104) {
        print("jq_tree: CHECKSUM:" ++ string(checksum) ++ "\n")
    } else {
        print("jq_tree: FAIL checksum=" ++ string(checksum) ++ "\n")
    }
    print("__TIMING__:" ++ string((t1 - t0) * 1000.0) ++ "\n")
}
