// S7.5.2v4: unresolved local admission warns and checks the invocation's T.
fn admit_candidate(seed: any as T, candidate: any) any^ {
    let value: T = candidate
    value
}
let good = admit_candidate(1, 2)
let bad = admit_candidate(1, "bad");
[good, bad is error, bad.message]
