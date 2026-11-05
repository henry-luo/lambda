// S2.3.1v2 / D3.4.1v2: one effective field, first position, last value/type.
import n: 'urn:lambda:duplicate-keys'
import alias: 'urn:lambda:duplicate-keys'
import other: 'urn:lambda:other-keys'

fn build(key) { {[key]: 1, b: 2, [key]: 3} }
let m = build("a")
let expected = {a: 3, b: 2}
let base = {a: 1, b: 2}
let override = {*:base, a: 3}
let reverse = {a: 3, *:base}
let multi = {*:base, *:{a: 3, c: 4}, ["b"]: 5}
let retyped = {["a"]: 1, b: 2, a: [7, 8]}
let nulled = {a: "old", b: 2, ["a"]: null}
let from_null = {a: null, b: 2, ['a']: "new"}
let widened = {a: true, b: 2, ["a"]: 3.5}
let unicode = {['e\u0301']: 1, 'é': 3}
let qualified = {[n.a]: 1, a: 2, [alias.a]: 3, [other.a]: 4}
let dotted = {'n.a': 1, [n.a]: 2}
let nested = {a: {a: 1}, b: {a: 2}}
let el = <node a: 1, b: 2, ["a"]: "last", "child">
let spread_element = <node *:map(el), a: "override", "new">;

[
  m.a == 3,
  len(m) == 2,
  m == expected,
  m == {b: 2, a: 3},
  [for (k at m) k] == ['a', 'b'],
  [for (v in m) v] == [3, 2],
  format(m, 'json')^ == format(expected, 'json')^,
  override == expected,
  reverse == base,
  base == {a: 1, b: 2},
  multi == {a: 3, b: 5, c: 4},
  [for (k at multi) k] == ['a', 'b', 'c'],
  len(retyped) == 2 and retyped == {a: [7, 8], b: 2},
  [for (k at retyped) k] == ['a', 'b'],
  nulled == {a: null, b: 2} and len(nulled) == 2,
  from_null == {a: "new", b: 2} and len(from_null) == 2,
  widened == {a: 3.5, b: 2} and len(widened) == 2,
  unicode['e\u0301'] == 1 and unicode['é'] == 3 and len(unicode) == 2,
  qualified[alias.a] == 3 and qualified.a == 2 and qualified[other.a] == 4,
  len(qualified) == 3,
  len(dotted) == 2 and dotted['n.a'] == 1 and dotted[n.a] == 2,
  nested.a.a == 1 and nested.b.a == 2,
  el.a == "last" and [for (k at el) k] == ['a', 'b'],
  content(el) == ["child"],
  spread_element.a == "override" and el.a == "last",
  [for (k at spread_element) k] == ['a', 'b'],
  [for (i in 1 to 8) build("a") == expected] == fill(8, true)
]
