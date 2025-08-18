import .test.demo.readability;

// Test script for Mozilla Readability-inspired content extraction
// Loads HTML and calls transform() to extract readable content

"=== Lambda Script Readability Demo ==="
"Inspired by Mozilla Readability"

// Load and parse the sample HTML article
"Loading sample article..."
let html_content = input('./test/demo/sample_article.html', 'html')

"HTML parsed successfully!"
html_content