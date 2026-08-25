// F0b end-to-end fixture. The checkbox's activation comes from the dom
// package's behavior template (the UA role); this page's own template is an
// author template that only listens for the `change` the behavior fires.
view <page> state changes: 0 {
  <div class:"wrap",
    <input type:"checkbox", class:"cb">
    if (changes > 0) {
      <span class:"changed", "yes">
    } else {
      <em class:"pristine", "no">
    }
  >
}
on change(evt) {
  changes = changes + 1
}

let doc = <page>
apply(doc)
