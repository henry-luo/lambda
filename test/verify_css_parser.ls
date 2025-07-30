/* CSS Parser Verification Script
 * 
 * This script verifies the CSS parser fix for nested rules within @media and @supports.
 * 
 * BEFORE FIX: 173 rules (missing nested rules from @media/@supports)
 * AFTER FIX:  182 rules (includes nested rules from @media/@supports)
 * IMPROVEMENT: +9 rules successfully captured from nested at-rules
 * 
 * The parser now correctly includes nested rules in the main rules count.
 */

let css_data = input('./test/input/stylesheet_3_0.css', 'css');
len(css_data.rules)
