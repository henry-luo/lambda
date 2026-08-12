// A braced for-expression body is a content block, not a for statement.
let transformed = for (x in [1, 2, 3]) {
    let doubled = x * 2;
    doubled + 1
}

transformed
