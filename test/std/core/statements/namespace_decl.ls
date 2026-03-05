// Test: Namespace Declaration
// Layer: 2 | Category: statement | Covers: namespace decl, namespaced elements, attributes

// ===== Basic namespace declaration =====
namespace html: 'http://www.w3.org/1999/xhtml'

// ===== Element with namespace =====
let doc = <html:div class: "container">
    <html:p> "Hello"
    <html:span> "World"
doc
name(doc)

// ===== Namespace prefix in attributes =====
namespace xml: 'http://www.w3.org/XML/1998/namespace'
let el = <div xml:lang: "en"> "Content"
el

// ===== Multiple namespaces =====
namespace svg: 'http://www.w3.org/2000/svg'
namespace xlink: 'http://www.w3.org/1999/xlink'
let svg_el = <svg:circle cx: "50", cy: "50", r: "25">
svg_el
name(svg_el)

// ===== Namespace in querying =====
let tree = <root>
    <item type: "a"> "first"
    <item type: "b"> "second"
    <item type: "a"> "third"
tree?item | map(fn(e) => str(e[0]))
