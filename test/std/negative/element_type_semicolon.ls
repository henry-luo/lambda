// Test: `;` has no role inside an element type
// Layer: 2 | Category: negative | Covers: S16.9.3 element separators
// An element pattern follows the literal's delimiters: nothing between a bare
// tag and its content, `,` between attributes and content. The type-pattern
// parser used to accept the retired `;` divider here.

type Item = <li; string>
let ok = <li "x"> is Item
ok
