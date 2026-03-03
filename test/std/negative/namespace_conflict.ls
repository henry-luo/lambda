// Test: Namespace Conflict
// Layer: 2 | Category: negative | Covers: variable name conflicts with namespace prefix

// Declare namespace
namespace html: 'http://www.w3.org/1999/xhtml'

// Attempt to use namespace prefix as variable - should produce error
let html = "conflict"
html
