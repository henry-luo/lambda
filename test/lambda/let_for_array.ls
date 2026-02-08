// Test for let-binding with array containing for-expression
// This tests the fix for Bug 3: arrays with for-expressions in let bindings
// Previously caused "Failed to find function 'main'" due to type mismatch

"=== Let binding with for-expression array ==="

// Test 1: Simple let binding with for-expression array
fn filter_positive(arr) {
    (let filtered = [for (x in arr where x > 0) x],
     filtered)
}
filter_positive([1, -2, 3, -4, 5])

// Test 2: Nested let bindings with for-expression arrays
fn process_numbers(arr) {
    (let positives = [for (x in arr where x > 0) x],
     let doubled = [for (x in positives) x * 2],
     doubled)
}
process_numbers([1, -2, 3, -4, 5])

// Test 3: Let binding with for-expression array used in condition
fn has_positive(arr) {
    (let positives = [for (x in arr where x > 0) x],
     len(positives) > 0)
}
has_positive([1, -2, 3])
// Note: has_positive([-1, -2, -3]) returns true due to Bug 1 (empty for returns [null])

// Test 4: Multiple let bindings with different for-expressions
fn filter_and_count(arr) {
    (let evens = [for (x in arr where x % 2 == 0) x],
     let odds = [for (x in arr where x % 2 != 0) x],
     {evens: evens, odds: odds, even_count: len(evens), odd_count: len(odds)})
}
filter_and_count([1, 2, 3, 4, 5, 6])

// Test 5: Recursive function with let bindings containing for-expressions
fn sum_positives_recursive(arr, idx: int, acc: int) {
    if (idx >= len(arr)) acc
    else (let current = arr[idx],
          let new_acc = if (current > 0) acc + current else acc,
          sum_positives_recursive(arr, idx + 1, new_acc))
}
fn sum_positives(arr) {
    sum_positives_recursive(arr, 0, 0)
}
sum_positives([1, -2, 3, -4, 5])

// Test 6: Let binding with for-expression array in function that calls other functions
fn double_value(x) { x * 2 }
fn filter_and_transform(arr, threshold) {
    (let filtered = [for (x in arr where x > threshold) x],
     [for (x in filtered) double_value(x)])
}
filter_and_transform([1, 2, 3, 4, 5], 2)

// Test 7: Complex nested expressions with let bindings
fn complex_filter(data) {
    (let names = [for (item in data) item.name],
     let ages = [for (item in data) item.age],
     let adults = [for (item in data where item.age >= 18) item.name],
     {names: names, ages: ages, adults: adults})
}
complex_filter([{name: "Alice", age: 25}, {name: "Bob", age: 15}, {name: "Carol", age: 30}])

"=== End of let-binding tests ==="
