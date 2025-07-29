// CSS Stylesheet Parser Test
"=== CSS Stylesheet Parser Test ==="

// Parse CSS stylesheet from file
let stylesheet = input('./test/input/stylesheet.css', 'css')
"Stylesheet structure:"
type(stylesheet)

// Show stylesheet has rules
"Number of CSS rules:"
len(stylesheet.rules)

// Access first rule (body element)
"First rule (body selector):"
stylesheet.rules[0].selectors
"First rule margin property:"
stylesheet.rules[0].margin
"First rule padding property:"
stylesheet.rules[0].padding

// Show @media at-rule parsing
"@media rule (index 4):"
stylesheet.rules[4]
"@media rule name:"
stylesheet.rules[4].name
"@media rule prelude:"
stylesheet.rules[4].prelude
"Number of nested rules in @media:"
len(stylesheet.rules[4].rules)

// Show @keyframes at-rule parsing  
"@keyframes rule (index 5):"
stylesheet.rules[5].name
"@keyframes nested rules count:"
len(stylesheet.rules[5].rules)

// Show multiple selectors parsing
"Multiple selectors rule (h1,h2,h3...):"
stylesheet.rules[6].selectors

// Show CSS functions are parsed as elements
"Complex background property:"
stylesheet.rules[7].background

"------------------------------\n"
stylesheet
"✅ CSS stylesheet parser test completed!"
