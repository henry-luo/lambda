// ESO42 fixture. The `change` that follows a blur is decided by the dom
// package's `commit` handler, not by native — this page's own template is an
// author template that only counts the `change` events that result.
// `seq` records the order events arrive: each change appends a 1, each blur a
// 2, so "change then blur" reads as 12 and the inverted order would read 21.
// (Digits rather than a string because Lambda has no string concatenation.)
view <page> state changes: 0, seq: 0 {
  <div class:"wrap",
    <input type:"text", class:"a">
    <input type:"text", class:"b">
    <span class:"count", string(changes)>
    <span class:"seq", string(seq)>
  >
}
on change(evt) {
  // F18 dispatches the producing template at target and bubble positions.
  // Count the target delivery so this fixture tests one committed DOM event.
  if (evt.event_phase == 2) {
    changes = changes + 1
    seq = seq * 10 + 1
  }
}
on blur(evt) {
  if (evt.event_phase == 2) {
    seq = seq * 10 + 2
  }
}

let doc = <page>
apply(doc)
