// RE2 repeats at most 1000 times; `{1001}` had left the pattern silently
// matching nothing.
type bad_limit = \("\d"{1001})
