// S8.2.2v6: local spelling plus resolved namespace; aliases are one identity.
import n: 'urn:lambda:keys'
import alias: 'urn:lambda:keys'
import other: 'urn:lambda:other'
let m = {[n.a]: 1, a: 2, [other.a]: 3, 'n.a': 4}
let e = <n.root n.a: "qualified", a: "global">
let x = parse("<p:root xmlns:p=\"urn:lambda:keys\" p:a=\"qualified\" a=\"global\"/>", 'xml')^
let root = x[0]
let constructed = <plain [symbol("a", "urn:lambda:keys")]: "dynamic">
let constructed_again = (parse(format(constructed,'xml')^,'xml')^)[0]
let xml = format(e, 'xml')^
let again = (parse(xml, 'xml')^)[0];
[n.a == alias.a, n.a != other.a, n.a != 'a',
 m[n.a], m[alias.a], m.a, m[other.a], m['n.a'], len(m),
 e[n.a], e[alias.a], e.a, name(e) == alias.root, e is <alias.root>,
 root[n.a], root.a, name(root) == alias.root,
 again[alias.a], again.a, name(again) == alias.root,
 [for (k,v at m) [k == n.a, k == other.a, v]], constructed_again[alias.a]]
