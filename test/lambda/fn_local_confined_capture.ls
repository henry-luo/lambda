// S9.1.4v2: confined sibling procedures resolve the same parent-local binding.
// S12.1.1v3 prevents invoking either pn while the parent fn runs.
fn owner(seed) {
    var value = seed
    pn increment() { value = value + 1 }
    pn replace() { value = 99 }
    value = value + 2
    return value
}
[owner(10), owner(20)]
