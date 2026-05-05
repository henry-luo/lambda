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

// ---------------------------------------------------------------------------
// CollabState: receive remote edits while local edits are unconfirmed.
// ---------------------------------------------------------------------------
let cs0 = collab_state(d, 10, text_selection(pos([0, 0], 0), pos([0, 0], 0)))
let cs1 = collab_local_step(cs0, step_replace_text([0, 0], 7, 12, "Lambda"), null)
"cs1 doc:";        doc_text(cs1.doc) == "Hello, Lambda.Second."
"cs1 pending:";    len(cs1.unconfirmed) == 1

let cs2 = collab_receive_remote(cs1, [step_replace_text([0, 0], 0, 0, "Hi! ")], 11)
"cs2 doc:";        doc_text(cs2.doc) == "Hi! Hello, Lambda.Second."
"cs2 base:";       doc_text(cs2.base_doc) == "Hi! Hello, world.Second."
"cs2 pending:";    len(cs2.unconfirmed) == 1
"cs2 rebased from:"; cs2.unconfirmed[0].from == 11
"cs2 version:";    cs2.version == 11
"cs2 dropped:";    cs2.dropped == 0
"cs2 selection:";  cs2.selection.anchor.offset == 4

let cs3 = collab_ack(cs2, 1, 12)
"cs3 pending:";    len(cs3.unconfirmed) == 0
"cs3 base:";       doc_text(cs3.base_doc) == doc_text(cs2.doc)
"cs3 doc:";        doc_text(cs3.doc) == doc_text(cs2.doc)
"cs3 version:";    cs3.version == 12
let cs4 = collab_ack(cs3, 5, 13)
"cs4 pending:";    len(cs4.unconfirmed) == 0
"cs4 version:";    cs4.version == 13

let cs_drop0 = collab_state(d, 20, node_selection([1]))
let cs_drop1 = collab_local_step(cs_drop0, step_replace_text([1, 0], 0, 7, "X"), null)
let cs_drop2 = collab_receive_remote(cs_drop1, [step_replace([], 1, 2, [])], 21)
"cs drop doc:";    doc_text(cs_drop2.doc) == "Hello, world."
"cs drop pending:"; len(cs_drop2.unconfirmed) == 0
"cs drop count:";  cs_drop2.dropped == 1
"cs drop selection path:"; path_equal(cs_drop2.selection.path, [])

// ---------------------------------------------------------------------------
// Collab packets: stable transport-neutral message shapes.
// ---------------------------------------------------------------------------
let pkt_local = collab_local_packet(cs1, "client-a")
"pkt local kind:"; pkt_local.kind == 'collab_local_steps'
"pkt local client:"; pkt_local.client_id == "client-a"
"pkt local base:"; pkt_local.base_version == 10
"pkt local steps:"; pkt_local.steps[0].text == "Lambda"

let pkt_remote = collab_remote_packet(11, [step_replace_text([0, 0], 0, 0, "Hi! ")])
let cs_pkt_remote = collab_apply_packet(cs1, pkt_remote)
"pkt remote doc:"; doc_text(cs_pkt_remote.doc) == doc_text(cs2.doc)
"pkt remote pending:"; len(cs_pkt_remote.unconfirmed) == 1
"pkt remote version:"; cs_pkt_remote.version == 11

let pkt_ack = collab_ack_packet(12, 1)
let cs_pkt_ack = collab_apply_packet(cs_pkt_remote, pkt_ack)
"pkt ack pending:"; len(cs_pkt_ack.unconfirmed) == 0
"pkt ack version:"; cs_pkt_ack.version == 12

let cs_unknown = collab_apply_packet(cs_pkt_ack, {kind: "noop"})
"pkt unknown same:"; doc_text(cs_unknown.doc) == doc_text(cs_pkt_ack.doc)

// ---------------------------------------------------------------------------
// Presence packets: collaborator cursors/ranges lower into decorations.
// ---------------------------------------------------------------------------
let pr_caret = collab_presence_packet("client-b", "Bea", "#f00",
  text_selection(pos([0, 0], 5), pos([0, 0], 5)))
let pr_caret_decos = collab_presence_decorations(pr_caret)
"presence caret count:"; len(pr_caret_decos.items) == 1
"presence caret kind:"; pr_caret_decos.items[0].kind == 'widget'
"presence caret class:"; pr_caret_decos.items[0].attrs.class == "collab-caret"
"presence caret render:"; pr_caret_decos.items[0].render.client_id == "client-b"

let pr_range = collab_presence_packet("client-c", "Cal", "#0af",
  text_selection(pos([0, 0], 12), pos([0, 0], 7)))
let pr_range_decos = collab_presence_decorations(pr_range)
"presence range kind:"; pr_range_decos.items[0].kind == 'inline'
"presence range from:"; pr_range_decos.items[0].from.offset == 7
"presence range to:"; pr_range_decos.items[0].to.offset == 12
"presence range class:"; pr_range_decos.items[0].attrs.class == "collab-selection"

let pr_node = collab_presence_packet("client-d", "Dee", "#090", node_selection([1]))
let pr_all = collab_presence_packet("client-e", "Eli", "#555", all_selection())
let pr_set = collab_presences_decorations([pr_caret, pr_range, pr_node, pr_all])
"presence set count:"; len(pr_set.items) == 4
"presence node kind:"; pr_set.items[2].kind == 'node'
"presence node path:"; path_equal(pr_set.items[2].path, [1])
"presence all path:"; path_equal(pr_set.items[3].path, [])
