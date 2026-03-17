// Pipe type inference tests
// Covers: pipe with ~ producing Array even when left side type is unknown (ANY)
// This was the root cause of garbage values in chart legend_width()

// --- Direct pipe with string/len on typed arrays ---

'=1='; ["cat", "fish", "elephant"] | string(~)
'=2='; ["cat", "fish", "elephant"] | len(~)
'=3='; ["cat", "fish", "elephant"] | len(string(~))
'=4='; [10, 5, 20] | string(~)

// --- Aggregation over pipe results ---

'=5='; max(["cat", "fish", "elephant"] | len(~))
'=6='; min(["cat", "fish", "elephant"] | len(~))
'=7='; sum(["cat", "fish", "elephant"] | len(~))

// --- Function with untyped parameter (the actual bug trigger) ---

fn widths(items) {
    items | len(string(~))
}

fn max_width(items) {
    max(items | len(string(~)))
}

fn to_strings(items) {
    items | string(~)
}

'=8='; widths(["cat", "fish", "elephant"])
'=9='; max_width(["cat", "fish", "elephant"])
'=10='; to_strings([10, 20, 30])

// --- Chained pipe with ~ and sysfunc in function context ---

fn transform(data) {
    data | ~ * 2 | string(~)
}

'=11='; transform([1, 2, 3])

// --- Pipe with ~ inside function, then aggregation ---

fn longest(words) {
    let lengths = words | len(~);
    max(lengths)
}

'=12='; longest(["a", "bb", "ccc", "dd"])
