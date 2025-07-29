"=== Advanced CSS Refactor Test ==="

let css_advanced = input('./test/input/stylesheet_3_0.css', 'css')

"Testing refactored structure on complex CSS:"
"============================================="

"First rule with refactored structure:"
css_advanced.rules[0]

"Direct access to 'box-sizing' property:"
css_advanced.rules[21]["box-sizing"]

"Direct access to 'background-color' property:"
css_advanced.rules[22]["background-color"]

"Transform property values:"
css_advanced.rules[48].transform

"Animation property values:"
css_advanced.rules[60].animation

"Grid template columns:"
css_advanced.rules[63]["grid-template-columns"]

"@font-face rule with refactored properties:"
css_advanced.rules[55]

"🎉 Advanced refactored CSS test completed!"
