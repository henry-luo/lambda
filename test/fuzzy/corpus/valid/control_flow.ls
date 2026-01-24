// Control flow
let score = 85;

// If expression
let grade = if (score >= 90) "A" 
    else if (score >= 80) "B" 
    else if (score >= 70) "C" 
    else "F";

// For expression
let squares = for (i in 1 to 10) i * i;
let doubled = for (x in [1, 2, 3, 4, 5]) x * 2;

// Conditional for
let positive = for (x in [-2, -1, 0, 1, 2]) if (x > 0) x else 0;

grade
