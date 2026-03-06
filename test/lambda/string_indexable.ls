// Test string-as-indexable behavior
// Strings are singular (not iterable) in collection functions

// ============================================================
// PASSTHROUGH FUNCTIONS
// ============================================================
'reverse passthrough'
{r: reverse("hello")}
{r: reverse("café")}

'sort passthrough'
{r: sort("hello")}
{r: sort("hello", "desc")}

'unique passthrough'
{r: unique("hello")}

'min passthrough'
{r: min("abc")}

'max passthrough'
{r: max("xyz")}

// ============================================================
// SUBSTRING OPERATIONS
// ============================================================
'take'
{r: take("hello", 3)}
{r: take("café", 3)}
{r: take("hello world", 5)}

'drop'
{r: drop("hello", 2)}
{r: drop("café", 3)}
{r: drop("hello world", 6)}

// ============================================================
// CONCAT (++ operator)
// ============================================================
'concat'
{r: "hello" ++ " world"}
{r: "foo" ++ "bar"}

// ============================================================
// CHARS DECOMPOSITION
// ============================================================
'chars'
{r: chars("hello")}
{r: chars("café")}
{r: chars("a")}

// ============================================================
// INDEXING (UTF-8 aware)
// ============================================================
'indexing'
{r: "hello"[0]}
{r: "hello"[4]}
{r: "café"[3]}
{r: "café"[0]}

// ============================================================
// SLICE (existing, should still work)
// ============================================================
'slice'
{r: slice("hello", 1, 4)}
{r: slice("café", 0, 4)}

// ============================================================
// RANGE SUBSCRIPT: str[a to b] (inclusive both ends)
// ============================================================
'range subscript'
{r: "hello"[0 to 4]}
{r: "hello"[1 to 3]}
{r: "hello"[0 to 0]}
{r: "hello"[4 to 4]}
{r: "café"[0 to 3]}
{r: "café"[2 to 3]}
{r: "hello world"[6 to 10]}

// range subscript with let variable
let s = "hello world"
{r: s[0 to 4]}
{r: s[6 to 10]}

// ============================================================
// SYMBOL RANGE SUBSCRIPT: sym[a to b] (inclusive both ends)
// ============================================================
'symbol range subscript'
{r: 'hello'[0 to 4]}
{r: 'hello'[1 to 3]}
{r: 'hello'[0 to 0]}
{r: 'hello'[4 to 4]}

// symbol range subscript with let variable
let sym = 'hello world'
{r: sym[0 to 4]}
{r: sym[6 to 10]}

'ALL TESTS COMPLETE'
