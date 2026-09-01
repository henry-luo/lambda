// The author template prevents the default action, so the dom package's
// checkbox behavior must not run — and therefore must not fire `change`.
view <page> state changes: 0, event_type: "" {
  <div class:"wrap",
    <input type:"checkbox", class:"cb">
    <span class:"event-type", event_type>
    if (changes > 0) {
      <span class:"changed", "yes">
    } else {
      <em class:"pristine", "no">
    }
  >
}
on click(evt) {
  event_type = evt.type
  return 'prevent-default'
}
on change(evt) {
  changes = changes + 1
}

let doc = <page>
apply(doc)
