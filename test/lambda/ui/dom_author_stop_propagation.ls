// F18: a template handler is an author-tier participant, not a first claimant.
// Stopping at the target prevents the same template from receiving the parent
// bubble position, while the record remains otherwise usable by the pipeline.
view <page> state calls: 0 {
  <div class:"author-parent",
    <button class:"author-target", "stop">
    <span class:"author-calls", calls>
  >
}
on click(evt) {
  calls = calls + 1
  if (evt.event_phase == 2) {
    evt.stop_propagation()
  }
  return 'handled'
}

let doc = <page>
apply(doc)
