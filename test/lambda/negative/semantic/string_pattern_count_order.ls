// A count `{n,m}` needs n <= m; the regex engine rejected `{5,2}` and the
// pattern had silently matched nothing.
type bad_order = \("\d"{5,2})
