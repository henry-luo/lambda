// Test: Stack Overflow (Should Crash)
// Category: negative
// Type: negative
// Expected: error

// This test should cause a stack overflow
let recursive_bomb = fn(n) {
    recursive_bomb(n + 1)
}

recursive_bomb(0)
