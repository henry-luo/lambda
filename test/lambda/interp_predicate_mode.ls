// P3: `that` clauses execute in the interpreter's pure, fuel-bounded mode.
type Positive = int that ~ > 0
type NonEmpty = string that len(~) > 0;

[
  3 is Positive,
  0 is Positive,
  "ok" is NonEmpty,
  "" is NonEmpty,
  match -1 {
    case int that ~ > 0: "positive"
    case int that ~ < 0: "negative"
    default: "other"
  }
]
