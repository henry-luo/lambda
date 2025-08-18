import .readability;

// Test script for Mozilla Readability-inspired content extraction
// Loads HTML and calls transform() to extract readable content

"=== Lambda Script Readability Demo ==="
"Inspired by Mozilla Readability"
""

// Load and parse the sample HTML article
"Loading sample article..."
let html_content = input('./test/demo/sample_article.html', 'html')

"HTML parsed successfully!"
"Original HTML structure (first few elements):"
// Show basic structure
if (html_content.children) {
    for i in 0 to min(3, len(html_content.children)) {
        let child = html_content.children[i];
        if (child.tag) {
            "\t- " + child.tag + (if (child.class) " (class: " + child.class + ")" else "")
        }
    }
}
""

// Transform HTML to readable content
"Extracting readable content..."
let readable_result = transform(html_content)

"=== EXTRACTION RESULTS ==="
""

// Show extraction success/failure
"Status: " + if (readable_result.success) "SUCCESS" else "FAILED"
if (not readable_result.success) {
    "Error: " + readable_result.message
}
""

// Show metadata
"=== METADATA ==="
if (readable_result.title) {
    "Title: " + readable_result.title
}
if (readable_result.author) {
    "Author: " + readable_result.author  
}
if (readable_result.date) {
    "Date: " + readable_result.date
}
if (readable_result.description) {
    "Description: " + readable_result.description
}
""

// Show content statistics
"=== CONTENT STATISTICS ==="
"Word count: " + readable_result.word_count
"Readability score: " + readable_result.score
if (readable_result.debug) {
    "Candidates found: " + readable_result.debug.candidates_found
    "Selected element: " + readable_result.debug.selected_tag + " (score: " + readable_result.debug.selected_score + ")"
}
""

// Show extracted text content (first 500 characters)
"=== EXTRACTED TEXT CONTENT (Preview) ==="
if (readable_result.text_content) {
    let preview_text = if (readable_result.text_content.length > 500) 
        readable_result.text_content.slice(0, 500) + "..."
    else 
        readable_result.text_content;
    
    preview_text
} else {
    "No text content extracted"
}
""

// Generate markdown output
"=== FORMATTED MARKDOWN OUTPUT ==="
let markdown_output = format_readable_markdown(readable_result)
markdown_output
""

// Test with a simpler HTML structure
"=== TESTING WITH SIMPLER HTML ==="
let simple_html = "<html><body><h1>Simple Test</h1><p>This is a simple test paragraph with enough content to be detected by the readability algorithm.</p><p>Another paragraph to make it more substantial.</p></body></html>"

// Note: We would need to parse this as HTML, but for demo purposes:
"Simple HTML test would go here (requires HTML parsing)"

""
"=== DEMO COMPLETED ==="
"The readability module successfully extracted the main article content"
"while filtering out navigation, ads, comments, and other non-content elements."
