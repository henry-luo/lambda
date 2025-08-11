// Test cases for heterogeneous array handling
// These were mentioned as problematic in the issue

// Test case 1: [1, null, 3]
let test1 = [1, null, 3]
test1[0]
test1[1] 
test1[2]

// Test case 2: [null, true, false, 123]
let test2 = [null, true, false, 123]
test2[0]
test2[1]
test2[2] 
test2[3]

// Test case 3: More complex heterogeneous array
let test3 = [42, "hello", null, true, 3.14]
test3

// Test case 4: Nested heterogeneous
let test4 = [[1, null], ["a", true]]
test4
