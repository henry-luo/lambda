// The DOM_F_ENGINE catalog rows, reached through `dom.*` (F32).
//
// State, focus and change-requests are host operations: their bodies live in
// the engine, above this module's link target, so each is reached through a
// weak provider seam -- the same shape as dom.load. Until they were wired, a
// script needed `radiant.*` for any of this, which is what kept the behaviour
// package on `radiant.*`; the engine forwards to the same body radiant.*
// publishes, so the two surfaces are one implementation.
import dom
import radiant

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
  },

  // The two surfaces are one implementation, not two that agree today: every
  // dom.* engine row forwards to the body radiant.* publishes. This is the
  // invariant the package migration rests on, so it is asserted rather than
  // assumed -- including where the answer is "not applicable" (a text control
  // with no UI state, an empty clipboard), because agreeing on absence is the
  // case a second implementation would most easily get wrong.
  agrees_with_radiant: [
    dom.get_state(first, "value") == radiant.get_state(first, "value"),
    dom.focused(first) == radiant.focused(first),
    dom.tc_value(first) == radiant.text_control(first),
    dom.clipboard_text() == radiant.clipboard_text(),
    dom.context_menu_target(first) == radiant.context_menu_target(first),
    dom.activate_popover(first) == radiant.activate_popover(first),
    dom.request_change(first) == radiant.request_change(first)
  ]
}
