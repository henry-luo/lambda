// S10.1.3: declaration blocks retain the enclosing pipe subject.
let direct = [5, 6] |> ({ let value = ~; value + 1 })
let handled = [5, 6] |> ({ let value = ~ ^ { 0 }; value })
let matched = [5, 6] |> ({ let value = ~; match value { case int: ~ + 1 } });
[direct, handled, matched]
