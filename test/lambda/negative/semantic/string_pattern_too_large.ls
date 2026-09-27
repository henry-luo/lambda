// A pattern the regex engine refuses is a compile error; this nesting
// exceeds RE2's repeat budget. It had been logged, and the pattern matched
// nothing.
type bad_size = \(((d{1000}){1000}){1000})
