// F3 fixture: constraint validation from the dom package. The CSS below only
// reacts to :invalid, so the assertions observe the state the package writes.
view <page> state n: 0 {
  <div class:"wrap",
    <style ".em:invalid { outline: 2px solid red; } .em:valid { outline: none; }">
    <input type:"email", class:"em", value:"">
  >
}

let doc = <page>
apply(doc)
