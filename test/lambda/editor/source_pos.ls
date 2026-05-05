// SourcePath / SourcePos resolution and comparison (Phase R2)
import lambda.package.editor.mod_source_pos

// ---------------------------------------------------------------------------
// pos() and selection constructors return well-formed records
// ---------------------------------------------------------------------------
let p1 = pos([0, 1], 3)
"p1 path len:";   len(p1.path)
"p1 path[0]:";    p1.path[0]
"p1 path[1]:";    p1.path[1]
"p1 offset:";     p1.offset

let ts = text_selection(pos([0], 0), pos([0], 5))
"ts kind:";       ts.kind
"ts anchor off:"; ts.anchor.offset
"ts head off:";   ts.head.offset

let ns = node_selection([1, 2])
"ns kind:";       ns.kind
"ns path len:";   len(ns.path)

let as = all_selection()
"as kind:";       as.kind

// ---------------------------------------------------------------------------
// path_compare / path_equal / path_is_prefix
// ---------------------------------------------------------------------------
"cmp [0] [0]:";       path_compare([0], [0])
"cmp [0] [1]:";       path_compare([0], [1])
"cmp [1] [0]:";       path_compare([1], [0])
"cmp [0,1] [0,2]:";   path_compare([0, 1], [0, 2])
"cmp [0] [0,0]:";     path_compare([0], [0, 0])
"cmp [0,0] [0]:";     path_compare([0, 0], [0])

"eq [0,1] [0,1]:";    path_equal([0, 1], [0, 1])
"eq [0,1] [0,2]:";    path_equal([0, 1], [0, 2])

"prefix [] of [0,1]:";        path_is_prefix([], [0, 1])
"prefix [0] of [0,1]:";       path_is_prefix([0], [0, 1])
"prefix [0,1] of [0,1]:";     path_is_prefix([0, 1], [0, 1])
"prefix [0,2] of [0,1]:";     path_is_prefix([0, 2], [0, 1])
"prefix [0,1,2] of [0,1]:";   path_is_prefix([0, 1, 2], [0, 1])

// ---------------------------------------------------------------------------
// pos_compare / pos_equal / pos_min / pos_max
// ---------------------------------------------------------------------------
let pa = pos([0, 1], 0)
let pb = pos([0, 1], 5)
let pc = pos([0, 2], 0)
"pos cmp pa pa:";     pos_compare(pa, pa)
"pos cmp pa pb:";     pos_compare(pa, pb)
"pos cmp pb pa:";     pos_compare(pb, pa)
"pos cmp pa pc:";     pos_compare(pa, pc)
"pos eq pa pa:";      pos_equal(pa, pa)
"pos eq pa pb:";      pos_equal(pa, pb)
"min pa pb off:";     pos_min(pa, pb).offset
"max pa pb off:";     pos_max(pa, pb).offset
"min pb pa off:";     pos_min(pb, pa).offset

// ---------------------------------------------------------------------------
// resolve_pos against a sample document
//   doc = <doc <paragraph "Hello"> <paragraph "World">>
// ---------------------------------------------------------------------------
let doc = <doc <paragraph; "Hello"> <paragraph; "World">>

let r0 = resolve_pos(doc, pos([], 0))
"r0 found:";       r0.found
"r0 depth:";       r0.depth

let r1 = resolve_pos(doc, pos([0], 0))
"r1 found:";       r1.found
"r1 depth:";       r1.depth
"r1 parent_idx:";  r1.parent_index
"r1 node tag:";    name(r1.node)

let r2 = resolve_pos(doc, pos([1, 0], 0))
"r2 found:";       r2.found
"r2 depth:";       r2.depth
"r2 parent_idx:";  r2.parent_index
"r2 node is string:"; type(r2.node) == string
"r2 node len:";    len(r2.node)

let r3 = resolve_pos(doc, pos([5], 0))
"r3 found:";       r3.found
"r3 depth:";       r3.depth

let r4 = resolve_pos(doc, pos([0, 0, 0], 0))
"r4 found:";       r4.found

// ---------------------------------------------------------------------------
// selection_to_string
// ---------------------------------------------------------------------------
"select same leaf:"; selection_to_string(doc, text_selection(pos([0, 0], 1), pos([0, 0], 4))) == "ell"
"select reversed:"; selection_to_string(doc, text_selection(pos([0, 0], 4), pos([0, 0], 1))) == "ell"
"select cross leaf:"; selection_to_string(doc, text_selection(pos([0, 0], 2), pos([1, 0], 3))) == "lloWor"
"select all:"; selection_to_string(doc, all_selection()) == "HelloWorld"
"select node:"; selection_to_string(doc, node_selection([1])) == "World"
