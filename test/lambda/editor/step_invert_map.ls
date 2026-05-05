// Step inversion and position mapping (Phase R3)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos

let d = node('doc', [
  node('paragraph', [text("Hello, world.")]),
  node('paragraph', [text("Second.")])
])

// ---------------------------------------------------------------------------
// invert(replace_text) round-trips
// ---------------------------------------------------------------------------
let s1 = step_replace_text([0, 0], 7, 12, "Lambda")
let d1 = step_apply(s1, d)
let inv1 = step_invert(s1, d)
"inv1 kind:";    inv1.kind
"inv1 from:";    inv1.from
"inv1 to:";      inv1.to
"inv1 text:";    inv1.text == "world"
let d_back = step_apply(inv1, d1)
"round-trip:";   doc_text(d_back) == doc_text(d)

// invert(insert) — inserting "[!] " inverts to deleting it
let s2 = step_replace_text([0, 0], 0, 0, "[!] ")
let d2 = step_apply(s2, d)
let inv2 = step_invert(s2, d)
"inv2 from:";    inv2.from
"inv2 to:";      inv2.to
"inv2 text:";    len(inv2.text) == 0
let d2b = step_apply(inv2, d2)
"insert undone:"; doc_text(d2b) == doc_text(d)

// invert(delete) — deleting reinserts the original substring
let s3 = step_replace_text([0, 0], 5, 12, "")
let d3 = step_apply(s3, d)
let inv3 = step_invert(s3, d)
"inv3 from:";    inv3.from
"inv3 to:";      inv3.to
"inv3 text:";    inv3.text == ", world"
let d3b = step_apply(inv3, d3)
"delete undone:"; doc_text(d3b) == doc_text(d)

// ---------------------------------------------------------------------------
// invert(replace) restores the original children
// ---------------------------------------------------------------------------
let s4 = step_replace([], 0, 2, [node('paragraph', [text("Replaced")])])
let d4 = step_apply(s4, d)
let inv4 = step_invert(s4, d)
"inv4 kind:";       inv4.kind
"inv4 slice len:";  len(inv4.slice)
let d4b = step_apply(inv4, d4)
"replace undone:";  doc_text(d4b) == doc_text(d)

let around_doc = node('doc', [
  node('paragraph', [text("before")]),
  node('paragraph', [text("keep")]),
  node('paragraph', [text("after")]),
  node('paragraph', [text("tail")])
])
let around = step_replace_around([], 0, 3, 1, 2, [node('blockquote', [])], 1)
let around_after = step_apply(around, around_doc)
let around_inv = step_invert(around, around_doc)
"around inv kind:"; around_inv.kind == 'replace'
"around inv to:"; around_inv.to == 2
let around_back = step_apply(around_inv, around_after)
"around undone:"; doc_text(around_back) == doc_text(around_doc)

// ---------------------------------------------------------------------------
// invert(add_mark) / invert(remove_mark)
// ---------------------------------------------------------------------------
let m = step_add_mark([0, 0], 'strong')
let dm = step_apply(m, d)
let m_inv = step_invert(m, d)
"m_inv kind:";   m_inv.kind == 'remove_mark'
let dm_back = step_apply(m_inv, dm)
"mark undone:";  len(dm_back.content[0].content[0].marks) == 0

// ---------------------------------------------------------------------------
// invert(set_attr) restores the previous value (or null when there was none)
// ---------------------------------------------------------------------------
let sa1 = step_set_attr([0], 'align', "center")
let da1 = step_apply(sa1, d)
let sa1_inv = step_invert(sa1, d)
"sa1 inv val null:"; sa1_inv.value == null
let da1b = step_apply(sa1_inv, da1)
"attr removed?";  attrs_get(da1b.content[0].attrs, 'align') == null
"attr removed count:"; len(da1b.content[0].attrs) == 0

let sa2 = step_set_attr([0], 'align', "right")
let da2 = step_apply(sa2, da1)  // start from "center"
let sa2_inv = step_invert(sa2, da1)
"sa2 inv val:";  sa2_inv.value == "center"
let da2b = step_apply(sa2_inv, da2)
"attr restored:"; attrs_get(da2b.content[0].attrs, 'align') == "center"

// invert(set_node_type)
let st = step_set_node_type([0], 'heading')
let dt = step_apply(st, d)
let st_inv = step_invert(st, d)
"st_inv tag:";   st_inv.tag == 'paragraph'
let dtb = step_apply(st_inv, dt)
"tag restored:"; dtb.content[0].tag == 'paragraph'

// ---------------------------------------------------------------------------
// step_map — position mapping through replace_text
// ---------------------------------------------------------------------------
//   leaf "Hello, world."   replace [7..12] "world" -> "Lambda" (5->6, +1 char)
let pre = pos([0, 0], 3)        // before from
let mid = pos([0, 0], 9)        // inside the affected range
let pst = pos([0, 0], 13)       // after the range (.)
"map pre:";  step_map(s1, pre).offset
"map mid:";  step_map(s1, mid).offset    // should snap to from + len(new_text) = 13
"map pst:";  step_map(s1, pst).offset    // shifted by +1 -> 14
"map text pre-bias:"; step_map_bias(s1, pos([0, 0], 7), -1).offset == 7
"map text post-bias:"; step_map_bias(s1, pos([0, 0], 7), 1).offset == 13
"map insert pre-bias:"; step_map_bias(s2, pos([0, 0], 0), -1).offset == 0
"map insert post-bias:"; step_map_bias(s2, pos([0, 0], 0), 1).offset == 4
let other = pos([1, 0], 3)
"map other:"; step_map(s1, other).offset // unaffected
"map other path:"; path_equal(step_map(s1, other).path, [1, 0])

// ---------------------------------------------------------------------------
// step_map — position mapping through replace (children)
// ---------------------------------------------------------------------------
// step_replace([], 1, 1, [hr])  inserts a child at index 1 (no deletion)
let s_ins = step_replace([], 1, 1, [node('hr', [])])
let pp_pre  = pos([], 0)        // strictly before insertion
let pp_at   = pos([], 1)        // at insertion point — post-bias shifts by +1
let pp_pst  = pos([], 2)        // after the insertion site — shifts by +1
"replace map parent pre:";  step_map(s_ins, pp_pre).offset == 0
"replace map parent at:";   step_map(s_ins, pp_at).offset == 2
"replace map parent post:"; step_map(s_ins, pp_pst).offset == 3
"replace map pre-bias:"; step_map_bias(s_ins, pp_at, -1).offset == 1
"replace map post-bias:"; step_map_bias(s_ins, pp_at, 1).offset == 2
let inside  = pos([1, 0], 3)    // inside child #1, which gets shifted to #2
let mapped_inside = step_map(s_ins, inside)
"replace map child shift:"; mapped_inside.path[0]
"replace map offset preserved:"; mapped_inside.offset == 3

// Mapping a position inside a deleted child collapses to the boundary
let s_del = step_replace([], 1, 2, [])
let inside_deleted = pos([1, 0], 3)
let collapsed = step_map(s_del, inside_deleted)
"collapsed depth:";  len(collapsed.path)
"collapsed offset:"; collapsed.offset == 1   // step.from + len(slice)
let s_rep_two = step_replace([], 1, 2, [node('hr', []), node('paragraph', [text("New")])])
"collapsed pre-bias:"; step_map_bias(s_rep_two, inside_deleted, -1).offset == 1
"collapsed post-bias:"; step_map_bias(s_rep_two, inside_deleted, 1).offset == 3

// replace_around preserves positions inside the gap and collapses deleted wrappers.
let gap_mapped = step_map(around, pos([1, 0], 2))
"around gap path:"; gap_mapped.path[0] == 1
"around gap offset:"; gap_mapped.offset == 2
let around_deleted = step_map(around, pos([2, 0], 1))
"around deleted depth:"; len(around_deleted.path) == 0
"around deleted offset:"; around_deleted.offset == 1
"around deleted pre-bias:"; step_map_bias(around, pos([2, 0], 1), -1).offset == 0
"around deleted post-bias:"; step_map_bias(around, pos([2, 0], 1), 1).offset == 1
let around_tail = step_map(around, pos([3, 0], 1))
"around tail path:"; around_tail.path[0] == 2

// Marks/attr/node-type don't move positions
"mark map identity:"; pos_equal(step_map(m, pre), pre)
"attr map identity:"; pos_equal(step_map(sa1, pre), pre)
"type map identity:"; pos_equal(step_map(st, pre), pre)
