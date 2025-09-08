// Test: Infinite Loop (Should Timeout)
// Category: negative
// Type: negative
// Expected: timeout

// This test should hang and be killed by timeout
let x = 0
x
while true {
    x = x + 1
}
