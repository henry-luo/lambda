// Phase 6: | is union everywhere; |> is pipe.

"type union expression:";
let T = int | string;
[5 is T, "x" is T, true is T]

"whole-value pipe:";
[1, 2, 3] |> sum

"mapping pipe:";
[1, 2, 3] |> ~ * 2

"call pipe chain:";
"a,b,c" |> split(",") |> join("-")
