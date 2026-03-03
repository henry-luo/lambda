// Test namespace v2: import-based namespaces with sub-map attribute desugaring
import svg: 'http://www.w3.org/2000/svg'
import xlink: 'http://www.w3.org/1999/xlink'

// =============================================
// 1. Basic namespace import and element tags
// =============================================
"1. namespaced elements"
<svg.rect>
<svg.circle cx: 50, cy: 50, r: 25>

// =============================================
// 2. Attribute desugaring: ns.attr → ns: {attr: val}
// =============================================
"2. single ns attr desugar"
<div xlink.href: "url">

"3. multiple ns attrs merge into one sub-map"
<svg.a xlink.href: "https://example.com", xlink.title: "Example">

"4. mixed regular and ns attrs"
<svg.a class: "link", xlink.href: "url", id: "a1">

// =============================================
// 3. Access namespaced attributes via sub-map
// =============================================
"5. access ns attr via sub-map"
let el = <svg.a xlink.href: "https://example.com", xlink.title: "Ex", class: "link">
[el.class, el.xlink.href, el.xlink.title]

"6. get namespace sub-map"
el.xlink

// =============================================
// 4. Map literal with explicit ns sub-map
// =============================================
"7. map with ns sub-map"
{xlink: {href: "url", title: "t"}}

"8. map ns sub-map access"
let m = {svg: {width: 100, height: 50}, class: "box"}
m.svg
[m.svg.width, m.svg.height]

// =============================================
// 5. Namespaced symbol literals
// =============================================
"9. ns.name qualified symbols"
svg.rect
svg.circle
xlink.href

"10. qualified symbol equality"
(svg.rect == svg.rect)
(svg.rect == svg.circle)

// =============================================
// 6. Element tag with namespace preserved
// =============================================
"11. element name preserved"
let r = <svg.rect>
[string(r)]
