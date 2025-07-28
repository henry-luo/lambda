let man = input('./test/input/test.man', 'man')

"Man page parsing successful!"

// Verify the document structure follows schema
let doc = man
doc.version  // Should be "1.0"
doc[0]       // Should be meta element
doc[1]       // Should be body element

"Schema validation completed!"

// Try accessing specific parsed elements
let body = doc[1]
body[0]      // First heading (NAME)
body[1]      // First paragraph

"Element access successful!"

man

"All man page tests completed!"
