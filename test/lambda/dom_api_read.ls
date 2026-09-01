// `dom.query_all` returns a snapshot, not a live collection (S9.2.2, ES36).
//
// The distinction is load-bearing: a Lambda caller iterating a query result
// walks the value it was handed, so mutating the tree part-way through cannot
// change the sequence under it. getElementsByTagName-style live collections are
// deliberately absent from this module for the same reason.
import radiant
import dom

let doc = radiant.load("test/js/dom_identity.html")
let root = radiant.root(doc)
let body = dom.closest(dom.query(root, "#intro"), "body")

// take the snapshot first, then grow the tree behind it
let before = dom.query_all(root, "p")
let count_before = len(before)
let extra = dom.clone(dom.query(root, "#intro"), true)
let _ = dom.append(body, extra)

// `before` still describes the tree as it was when the query ran, while a
// fresh query sees the added element
let snapshot_tags = [for (n in before) dom.tag(n)]

{
  count_at_query_time: count_before,
  snapshot_len_after_mutation: len(before),
  snapshot_tags: snapshot_tags,
  fresh_query_len: len(dom.query_all(root, "p")),
  first_still_reachable: dom.tag(dom.query(root, "#intro"))
}
