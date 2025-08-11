// First batch of fixes for comprehensive test
// Fixing the basic for expression syntax issues

// === BASIC FOR EXPRESSION FIXES ===

// Fixed for expressions with arrays - add parentheses around loop vars
(for (x in [1, 2, 3]) x * 2)
(for (item in ["a", "b", "c"]) item + "!")
(for (num in [1, 2, 3, 4, 5]) (if (num % 2 == 0) num else 0))

// Fixed for expressions with ranges - add parentheses around loop vars  
(for (i in 1 to 5) i * i)
(for (j in 0 to 3) j + 10)

// For statements with arrays - these were already correct
for item in [4, 5, 6] {
    item * 2
}

for i in 1 to 10 {
    i + 5
}

// === BASIC IF EXPRESSION FIXES ===

// Fixed if expressions - ensure 'else' is present
(if (true) "yes" else "no")
(if (1 > 0) 42 else 0)  
(if (false) null else "default")

// Fixed nested if expressions
(let choice = 1, if (choice == 1) 42 else if (choice == 2) "string" else "other")
