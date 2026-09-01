// F0b end-to-end fixture. The checkbox's activation comes from the dom
// package's behavior template (the UA role); this page's own template claims
// click first, so the fixture pins ES29's explicit-cancel requirement.
view <page> state changes: 0, author_clicks: 0 {
  <div class:"wrap",
    <input type:"checkbox", class:"cb">
    <span class:"author-clicks", author_clicks>
    if (changes > 0) {
      <span class:"changed", "yes">
    } else {
      <em class:"pristine", "no">
    }
  >
}
on click(evt) {
  author_clicks = author_clicks + 1
  return 'handled'
}
on change(evt) {
  changes = changes + 1
}

let doc = <page>
apply(doc)
