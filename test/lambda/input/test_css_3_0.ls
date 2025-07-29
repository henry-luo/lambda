// CSS Level 3 Comprehensive Parser Test

"=== CSS Level 3 Comprehensive Parser Test ==="

// Parse the comprehensive CSS3 stylesheet
let css3_stylesheet = input('./test/input/stylesheet_3_0.css', 'css')

"CSS3 Stylesheet structure:"
type(css3_stylesheet)

"Total number of CSS rules:"
len(css3_stylesheet.rules)

// Test CSS3 Selectors parsing
"=== CSS3 Selectors ==="
"Attribute selector [type=\"text\"]:"
css3_stylesheet.rules[0].selectors[0]

"Pseudo-class :nth-child(odd):"
css3_stylesheet.rules[4].selectors[0]

"Pseudo-element ::before:"
css3_stylesheet.rules[16].selectors[0]

// Test CSS3 Properties and Values
"=== CSS3 Properties ==="
"Box-sizing values:"
css3_stylesheet.rules[21].declarations[0].values

"Multiple background values:"
css3_stylesheet.rules[22].declarations[0].values

"Border-radius values:"
css3_stylesheet.rules[27].declarations[0].values
css3_stylesheet.rules[27].declarations[1].values
css3_stylesheet.rules[27].declarations[2].values

"Box-shadow with multiple shadows:"
css3_stylesheet.rules[27].declarations[12].values

// Test CSS3 Color Functions
"=== CSS3 Colors ==="
"RGBA color function:"
css3_stylesheet.rules[36].declarations[0].values[0]

"HSLA color function:"
css3_stylesheet.rules[36].declarations[2].values[0]

// Test CSS3 Transforms
"=== CSS3 Transforms ==="
"Transform translate function:"
css3_stylesheet.rules[48].declarations[0].values[0]

"Transform scale function:"
css3_stylesheet.rules[48].declarations[3].values[0]

"Transform rotate function:"
css3_stylesheet.rules[48].declarations[6].values[0]

"Combined transform functions:"
css3_stylesheet.rules[48].declarations[13].values

"3D Transform translate3d:"
css3_stylesheet.rules[48].declarations[19].values[0]

// Test CSS3 Animations and Keyframes
"=== CSS3 Animations ==="
"@keyframes fadeIn rule:"
css3_stylesheet.rules[57].name
"Keyframes rule structure:"
type(css3_stylesheet.rules[57])

"Animation property values:"
css3_stylesheet.rules[60].declarations[0].values

// Test CSS3 Flexbox
"=== CSS3 Flexbox ==="
"Flexbox display values:"
css3_stylesheet.rules[61].declarations[0].values
css3_stylesheet.rules[61].declarations[1].values

"Justify-content values:"
css3_stylesheet.rules[61].declarations[11].values
css3_stylesheet.rules[61].declarations[12].values
css3_stylesheet.rules[61].declarations[13].values

"Flex item properties:"
css3_stylesheet.rules[62].declarations[0].values
css3_stylesheet.rules[62].declarations[1].values

// Test CSS3 Grid
"=== CSS3 Grid ==="
"Grid display values:"
css3_stylesheet.rules[63].declarations[0].values

"Grid template columns with functions:"
css3_stylesheet.rules[63].declarations[2].values
css3_stylesheet.rules[63].declarations[3].values

// Test CSS3 Media Queries
"=== CSS3 Media Queries ==="
"Media query with complex conditions:"
css3_stylesheet.rules[66].name
css3_stylesheet.rules[66].prelude

// Test CSS3 Gradients
"=== CSS3 Gradients ==="
"Linear gradient function:"
css3_stylesheet.rules[71].declarations[0].values[0]

"Radial gradient function:"
css3_stylesheet.rules[71].declarations[3].values[0]

"Repeating gradient function:"
css3_stylesheet.rules[71].declarations[6].values[0]

// Test CSS3 Filters
"=== CSS3 Filters ==="
"Filter blur function:"
css3_stylesheet.rules[72].declarations[0].values[0]

"Multiple filter functions:"
css3_stylesheet.rules[72].declarations[12].values

// Test CSS3 Calc Function
"=== CSS3 Calc Function ==="
"Calc function in width:"
css3_stylesheet.rules[74].declarations[0].values[0]

"Complex calc expression:"
css3_stylesheet.rules[74].declarations[4].values[0]

// Test CSS3 Custom Properties (Variables)
"=== CSS3 Custom Properties ==="
":root selector with custom properties:"
css3_stylesheet.rules[75].selectors[0]

"Custom property declaration:"
css3_stylesheet.rules[75].declarations[0].property
css3_stylesheet.rules[75].declarations[0].values

"Var function usage:"
css3_stylesheet.rules[76].declarations[0].values[0]
css3_stylesheet.rules[76].declarations[1].values[0]

// Test CSS3 Feature Queries (@supports)
"=== CSS3 Feature Queries ==="
"@supports rule:"
css3_stylesheet.rules[77].name
css3_stylesheet.rules[77].prelude

"Complex @supports with 'and':"
css3_stylesheet.rules[79].prelude

// Summary
"=== Test Summary ==="
"✅ CSS Level 3 parser successfully handles:"
"• Advanced selectors (attribute, pseudo-classes, pseudo-elements)"
"• CSS3 properties (transforms, animations, flexbox, grid)"
"• CSS3 functions (calc, var, gradients, filters)"
"• CSS3 at-rules (@keyframes, @media, @supports, @font-face)"
"• CSS3 values (colors, measurements, keywords)"
"• Complex nested structures and multiple values"

css3_stylesheet

"🎉 CSS Level 3 comprehensive parser test completed!"
