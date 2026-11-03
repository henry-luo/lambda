// S12.1.4v4: local state does not change a polymorphic helper's colour.
function counts(f: function, x) any^ { var y = x; f(y) }
counts((x) => x + 1, 3)^
