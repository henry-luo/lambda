// S2.3.1v2 / S8.2.2v6: namespace aliases resolve to one key identity.
import n: 'urn:lambda:duplicate-keys'
import alias: 'urn:lambda:duplicate-keys'
let direct = <node n.a: 1, alias.a: 3>
let spread = <node n.a: 1, *:{b: 2}, alias.a: 3>
let computed = <node n.a: 1, ["b"]: 2, alias.a: 3>
"bound:duplicate-namespace-keys"
