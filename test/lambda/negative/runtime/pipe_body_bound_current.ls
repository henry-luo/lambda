// a match body inferred as any needs the dynamic callable check.
// @expect-error: E212
[1, 2] |> match (5) { case int: ~ * 10 default: 0 }
