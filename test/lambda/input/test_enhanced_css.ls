// Enhanced CSS Parser Test
"=== Enhanced CSS Parser Test ==="

// Test parsing the complete CSS grammar file
let complete_stylesheet = input('./test/input/complete_css_grammar.css', 'css')

"Complete CSS Grammar Stylesheet:"
type(complete_stylesheet)

if complete_stylesheet {
    "Number of rules: " + size(complete_stylesheet.rules)
    
    if size(complete_stylesheet.rules) > 0 {
        "First rule:"
        let first_rule = complete_stylesheet.rules[0]
        first_rule
        
        if first_rule.selector {
            "First rule selector: " + first_rule.selector
        }
        
        if first_rule.properties {
            "First rule properties: " + size(first_rule.properties)
        }
    }
    
    // Test specific new features
    "Testing new CSS units..."
    
    // Test simple CSS with new features
    let simple_css = "
        .test {
            width: 10cap;
            height: 5ic;
            margin: 2svw 3lvh;
            color: oklch(0.5 0.2 180deg);
            background: hwb(120deg 20% 30%);
            border-color: lab(50% 20 -30);
            font-size: 2rem;
            transform: rotate(45deg);
            border-radius: 1cqw;
        }
    "
    
    let test_stylesheet = input(simple_css, 'css', 'text')
    
    "Simple enhanced CSS test:"
    type(test_stylesheet)
    
    if test_stylesheet {
        "Simple test rules: " + size(test_stylesheet.rules)
    }
    
    "✅ Enhanced CSS parser test completed!"
} else {
    "❌ Failed to parse complete CSS grammar file"
}