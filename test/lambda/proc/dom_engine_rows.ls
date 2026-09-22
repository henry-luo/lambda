// The DOM_F_ENGINE catalog rows, reached through `dom.*` (F32).
//
// State, focus and change-requests are host operations: their bodies live in
// the engine, above this module's link target, so each is reached through a
// weak provider seam -- the same shape as dom.load. Until they were wired, a
// script needed `radiant.*` for any of this, which is what kept the behaviour
// package on `radiant.*`. This began by asserting that each row agreed with its
// `radiant.*` spelling; those spellings are retired now, so it pins the answers.
import dom

let doc = dom.load("test/lambda/dom/value_state.html")
let inputs = dom.query_selector_all(doc, "input")
let first = inputs[0]

{
  inputs_found: len(inputs),
  seeded_value: dom.get_state(first, "value"),
  focused_initially: dom.focused(first),
  request_change: dom.request_change(first),
  set_then_get: {
    let _ = dom.set_state(first, "value", "written by lambda")
    dom.get_state(first, "value")
  }
}
