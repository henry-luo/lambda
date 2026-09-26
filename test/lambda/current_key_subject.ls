// S10.1.3: `~key` belongs to the innermost `~`. A body that binds `~` to one
// subject -- a match arm, a constraint, a `that` proviso, a handler's value
// arm, a method -- binds no key, so `~key` is null there, inside a pipe or
// not. The JIT had left an enclosing pipe's index visible (and no register at
// all outside a pipe), T0 dereferenced a missing slot in a value arm, and the
// JIT's value arm outranked a nested pipe's own `~`.

"1 match arm";
[match 5 { case int: ~key }];
[5, 6] |> match ~ { case int: ~key }

"2 constraint";
type K = int that ~key == null;
[5 is K];
[5, 6] |> (~ is K);
[5, 6] |> match ~ {
  case int that ~key == null: "no key"
  default: "keyed"
}

"3 proviso";
[5 that ~key == null];
[5, 6] |> (~ that ~key == null)

"4 handler value arm";
let v1 = 1 ^ {0} ~ { ~key };
[v1];
[5, 6] |> (~ ^ {0} ~ { ~key })
let v2 = [1, 2] ^ {0} ~ { ~ |> ~ * 10 };
[v2]

"5 method";
type Box { x: int, fn key() => ~key };
let b = <Box x: 1>;
[b.key()]

"6 the pipe's own key returns after each body";
[5, 6] |> [~key, match ~ { case int: ~key }, ~key]

"7 no walk binds a key";
[~key]
