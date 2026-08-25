// F2 fixture: the dom package's <select> behavior template owns opening and
// closing the dropdown; the overlay itself stays native.
view <page> state n: 0 {
  <div class:"wrap",
    <select class:"sel",
      <option value:"a", "Apple">
      <option value:"b", "Banana">
    >
  >
}

let doc = <page>
apply(doc)
