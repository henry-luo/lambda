// D4.3.1/S11.4.6v4: an inferred first literal restricts neither later items
// nor storage, while an explicit receiving refinement still checks them all.
type Word = "a"
fn only_a(values: Word[]) any => values[0]
let words = ["a", "b"];
[words, only_a(words) is error, join(words, "")]
