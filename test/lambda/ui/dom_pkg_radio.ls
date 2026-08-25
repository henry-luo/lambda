// F1 fixture: radio-group exclusivity driven by the dom package's behavior
// template — selecting one peer must clear the previously selected one.
view <page> state changes: 0 {
  <div class:"wrap",
    <input type:"radio", name:"pick", class:"r1", value:"a">
    <input type:"radio", name:"pick", class:"r2", value:"b">
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
