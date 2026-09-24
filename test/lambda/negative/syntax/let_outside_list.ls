// @expect-error: E100
// @description: S2.5.4v2: a 'let' binds as an expression only inside a
// parenthesized list; an array literal is not one

let pairs = [let x = 1, x]
