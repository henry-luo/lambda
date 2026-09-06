// A module-defined element literal with a spread or computed key must be built
// under the MODULE's TypeElmt: elmt_literal_begin used to resolve the literal's
// type_index against the running script's type list, so with a big enough
// type table here the module literal silently took this script's type #N
// (13-field record -> null data -> segfault in fn_map_set on every tier).
import m: .mod_elmt_literal
type Wide = {a: int, b: int, c: int, d: int, e: int, f: int, g: int, h: int,
  i: int, j: int, k: int, l: int, n: int}
let filler: Wide = {a:1, b:2, c:3, d:4, e:5, f:6, g:7, h:8, i:9, j:10, k:11, l:12, n:13}
let g = <graph id:"g1", kind:"state", "child">
let spread = m.spread_graph(g)
let keyed = m.keyed_node(g, "extra")
let km = m.keyed_map({x: 1}, "y");
[spread.id, spread.'ir-stage', len(spread), keyed.kind, keyed.extra, km.y, filler.n]
