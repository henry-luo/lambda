// S8.2.2v6: namespace scope, default reset, aliases and detached XML serialization.
import n: 'urn:lambda:one'
import two: 'urn:lambda:two'
fn root(source) => (parse(source, 'xml')^)[0]
let r = root("<r xmlns=\"urn:lambda:one\" xmlns:p=\"urn:lambda:one\"><p:a p:k=\"one\" k=\"plain\"/><r xmlns=\"\"><x/></r><p:a xmlns:p=\"urn:lambda:two\" p:k=\"two\"/></r>")
let detached = root(format(r[0], 'xml')^)
let roundtrip = root(format(r, 'xml')^);
[name(r) == n.r, name(r[0]) == n.a, r[0][n.k], r[0].k,
 name(r[1]) == 'r', name(r[1][0]) == 'x', name(r[2]) == two.a, r[2][two.k],
 detached[n.k], detached.k, name(detached) == n.a,
 name(roundtrip[0]) == n.a, name(roundtrip[1]) == 'r', name(roundtrip[2]) == two.a,
 n.k at r[0], two.k at r[0], 'k' at r[0]]
