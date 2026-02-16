// Test: JSON-LD parsing in HTML
// Verifies that <script type="application/ld+json"> content is parsed as structured data

let doc = input("test/input/test_jsonld.html", 'html')?

// navigate to <html> -> <head>
let html = doc[1]
let head = html[0]

// find the script element (after whitespace text nodes)
// head children: "\n", <title>, "\n", <script type="application/ld+json">, "\n"
let script = head[3]
[name(script), script.type]

// the child of script should be a parsed JSON map, not a raw string
let jsonld = script[0]
type(jsonld)

// verify JSON-LD fields
[jsonld["@context"], jsonld["@type"], jsonld["name"], jsonld["url"]]
