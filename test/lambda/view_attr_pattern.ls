// F0b: element-pattern attribute predicates and engine-backed (bare) state names

// Test 1: value predicate matches its own spelling
view <input type:'checkbox'> state a: 0 {
  "checkbox-template"
}
apply(<input type:'checkbox'>)
0

// Test 2: value predicate rejects a different value (falls through, no template)
view <btn kind:'submit'> state b: 0 {
  "submit-template"
}
apply(<btn kind:'reset'>)
0

// Test 3: a literal predicate is compared by text across symbol and string,
// so a parsed-HTML string attribute matches a Lambda symbol literal
view <field mode:'edit'> state c: 0 {
  "edit-template"
}
apply(<field mode:"edit">)
0

// Test 4: a typed field is a presence predicate — it matches any value
view <link href: any> state d: 0 {
  "has-href"
}
apply(<link href:'/somewhere'>)
0

// Test 5: presence predicate does not match when the attribute is absent
apply(<link>)
0

// Test 6: a value predicate outranks a presence predicate on the same tag
view <cell kind: any> state e: 0 {
  "generic-cell"
}
view <cell kind:'header'> state f: 0 {
  "header-cell"
}
apply(<cell kind:'header'>)
0

// Test 7: and the presence predicate still wins for other values
apply(<cell kind:'body'>)
0

// Test 8: bare state names declare engine-backed bindings (no initializer),
// and may be mixed with initialized template-local state
view <widget> state checked, label: "ready" {
  label
}
apply(<widget>)
0

// Test 9: a bare state name reads as null until a host writes it
view <gauge> state level {
  string(level == null)
}
apply(<gauge>)
