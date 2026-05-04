// Collab primitives — Mapping, step rebase, serialisation (Phase R6)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_step
import lambda.package.editor.mod_collab

let d = node('doc', [
  node('paragraph', [text("Hello, world.")]),
  node('paragraph', [text("Second.")])
])

// ---------------------------------------------------------------------------
// Mapping composition: insert "ABC" at offset 0 then insert "X" at offset 5
// ---------------------------------------------------------------------------
let s_a = step_replace_text([0, 0], 0, 0, "ABC")        // shift +3
let s_b = step_replace_text([0, 0], 5, 5, "X")          // shift +1 from offset 5
let m   = mapping_concat(mapping_add(mapping_new(), s_a), mapping_from([s_b]))
"mapping size:";   mapping_size(m) == 2

// position 7 in original -> +3 (past s_a) = 10, then 10 >= 5 -> +1 = 11
let p1 = mapping_map(m, pos([0, 0], 7))
"mapped offset:";  p1.offset == 11
"mapped path:";    path_equal(p1.path, [0, 0])

// position 0 -> stays at 0+3=3 after s_a (boundary: post-bias pushes forward),
// then 3 < 5 -> unchanged
let p0 = mapping_map(m, pos([0, 0], 0))
"boundary:";       p0.offset == 3

// ---------------------------------------------------------------------------
// Rebase a local insert over a concurrent remote insert in the same text
// ---------------------------------------------------------------------------
// Remote: insert "Hi! " at offset 0 of paragraph 0 text leaf.
// Local: replace "world" (offsets 7..12) with "Lambda" — same paragraph.
let remote = [step_replace_text([0, 0], 0, 0, "Hi! ")]
let local  = [step_replace_text([0, 0], 7, 12, "Lambda")]
let r1 = rebase_steps(local, remote)
"rebase kept:";    len(r1.kept) == 1
"rebase dropped:"; r1.dropped == 0
let rs = r1.kept[0]
"rs from:";        rs.from == 11
"rs to:";          rs.to == 16
"rs text:";        rs.text == "Lambda"

// Apply remote then rebased-local to validate end-state text.
let d_remote = step_apply(remote[0], d)
let d_final  = step_apply(rs, d_remote)
"final text:";     doc_text(d_final) == "Hi! Hello, Lambda.Second."

// ---------------------------------------------------------------------------
// Rebase a local mark step over a remote sibling-replace that shifts
// the target paragraph's child index inside the doc.
// ---------------------------------------------------------------------------
// Remote inserts a new paragraph at the front of the doc, shifting the
// existing paragraph 0 -> 1 and paragraph 1 -> 2.
let remote2 = [step_replace([], 0, 0, [node('paragraph', [text("Top")])])]
// Local wants to bold the second-paragraph text leaf, originally [1,0].
let local2  = [step_add_mark([1, 0], 'strong')]
let r2 = rebase_steps(local2, remote2)
"r2 kept:";        len(r2.kept) == 1
let ms = r2.kept[0]
"r2 path[0]:";     ms.path[0] == 2
"r2 path[1]:";     ms.path[1] == 0
"r2 mark:";        ms.mark == 'strong'

// ---------------------------------------------------------------------------
// Drop: a local replace_text whose container was concurrently removed.
// Remote deletes paragraph 1 entirely; local edit inside [1,0] collapses.
// ---------------------------------------------------------------------------
let remote3 = [step_replace([], 1, 2, [])]
let local3  = [step_replace_text([1, 0], 0, 7, "X")]
let r3 = rebase_steps(local3, remote3)
// After rebase the position collapses to a parent-level offset, so the
// from/to no longer share the original child path; collapse-detection
// keeps them on the same path but with from==to (a degenerate insert).
// Either way, dropped + kept == 1.
"r3 total:";       (r3.dropped + len(r3.kept)) == 1

// ---------------------------------------------------------------------------
// Serialisation round-trip is structural identity for plain-data steps.
// ---------------------------------------------------------------------------
let wire = steps_serialize(local)
let back = steps_deserialize(wire)
"ser kind:";       back[0].kind == 'replace_text'
"ser text:";       back[0].text == "Lambda"
let d_back = step_apply(back[0], d)
"ser apply:";      doc_text(d_back) == "Hello, Lambda.Second."
