// sort() advanced tests — key function and options map

// Test 1: Sort by key function (ascending)
let users = [{name: "Charlie", age: 30}, {name: "Alice", age: 25}, {name: "Bob", age: 35}]
sort(users, (u) => (u.age))

// Test 2: Sort by key function on strings (by length)
sort(["banana", "apple", "cherry", "fig"], (w) => (len(w)))

// Test 3: Sort with options map — descending by key
sort(users, {by: (u) => (u.age), dir: 'desc'})

// Test 4: Sort with direction only
sort([3, 1, 4, 1, 5, 9], 'desc')

// Test 5: Sort ascending (default)
sort([5, 3, 1, 4, 2])
