// Test namespace infrastructure features
// Symbols have Target* ns field (null for unqualified), and support
// type checking, comparison, string operations, and use as element tags.

// =============================================
// Section 1: Namespace declarations
// =============================================
namespace svg: 'http://www.w3.org/2000/svg', xlink: 'http://www.w3.org/1999/xlink'

"1. namespaced element tags"
<svg.rect>
<svg.circle>

"2. namespaced attributes"
<svg.rect svg.width: 100, svg.height: 50>
<image xlink.href: "#id">

"3. mixed namespaced and regular"
<svg.rect id: "myRect", svg.width: 100>

// =============================================
// Section 2: Symbol literals and identity
// =============================================
"4. symbol literals"
'hello'
'world'
'multi-word-symbol'

"5. symbol equality"
('hello' == 'hello')
('hello' == 'world')
('abc' == 'abc')

"6. symbol type checks"
type('hello')
('hello' is symbol)
('hello' is string)
(123 is symbol)

// =============================================
// Section 3: Symbol operations
// =============================================
"7. concatenation preserves symbol type"
'hel' ++ 'lo'
'foo' ++ '-' ++ 'bar'

"8. symbol length"
len('hello')
len('a')

"9. symbol ordering"
('abc' < 'def')
('def' > 'abc')
('abc' <= 'abc')

// =============================================
// Section 4: Symbol string functions
// =============================================
"10. starts_with / ends_with"
starts_with('hello-world', 'hello')
ends_with('hello-world', 'world')

"11. index_of"
index_of('hello-world', '-')
index_of('namespace', 'space')

"12. replace"
replace('hello-world', '-', '_')

// =============================================
// Section 5: Symbol in collections
// =============================================
"13. symbol array"
['hello', 'world', 'test']
('hello' in ['hello', 'world'])

"14. map with symbol values"
{tag: 'div', ns: 'html'}

// =============================================
// Section 6: Symbol vs String distinction
// =============================================
"15. type distinction"
let sym = 'test'
let str = "test"
(sym is symbol)
(str is string)
(sym is string)
(str is symbol)

// =============================================
// Section 7: Element creation
// =============================================
"16. basic elements"
<div>
<span>

"17. element with attributes"
<div class: "main">
<span id: "test", class: "bold">

"18. element with content"
<p; "Hello world">
<div; "text"; 123>

"19. nested elements"
<ul; <li; "item1">; <li; "item2">>

// =============================================
// Section 8: Symbol in functions
// =============================================
"20. symbol as function param/return"
fn make_tag(prefix, name) { prefix ++ '-' ++ name }
make_tag('svg', 'rect')
make_tag('html', 'div')

"21. symbol in conditional"
let mode = 'dark'
[if (mode == 'dark') "dark mode" else "light mode"]

// =============================================
// Section 9: Symbol interning
// =============================================
"22. symbol interning"
let s1 = 'interned'
let s2 = 'interned'
(s1 == s2)
let s3 = 'different'
(s1 == s3)
// =============================================
// Section 10: Namespace member access (e.ns.attr)
// =============================================
"23. e.ns.attr member expression"
let elem = <svg.rect svg.width: 100, svg.height: 50>
elem.svg.width
elem.svg.height

"24. ns.value becomes qualified symbol"
svg.rect
svg.circle
xlink.href

"25. compare qualified symbols"
// svg.rect has namespace target, 'svg.rect' does not
// so they are not equal (different namespace identity)
(svg.rect == svg.rect)
(svg.rect == svg.circle)
// but string representation is the same
(string(svg.rect) == "svg.rect")
