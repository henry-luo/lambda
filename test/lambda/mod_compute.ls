// module with computed pub let variables (expressions, not just literals)
pub let doubled = 21 * 2
pub let label = "version-" ++ (3)
pub let total = len([1, 2, 3, 4, 5])
pub let nums = [for (i in 1 to 5) i * 10]

pub fn describe() { label ++ ":" ++ (total) }
