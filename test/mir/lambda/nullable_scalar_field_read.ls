// S7.1.1v3: a nullable record receiver preserves null when its scalar field
// is read; a present receiver uses the admitted packed field lane.
type Reading = {score: float, flag: bool}
type OptionalReading = {flag: bool?}
type Skewed = {flag: bool, score: float}

pn read_flag(value: Reading?) any { return value.flag }
pn read_score(value: Reading?) any { return value.score }
pn read_optional_flag(value: OptionalReading) any { return value.flag }
pn read_skewed_score(value: Skewed) any { return value.score }

pn main() {
    let present: Reading = {score: 2.5, flag: false}
    print([read_flag(present), read_flag(null),
           read_score(present), read_score(null)])
    print("\n")
    let optional: OptionalReading = {flag: null}
    let skewed: Skewed = {flag: true, score: 1.25}
    print([read_optional_flag(optional), read_skewed_score(skewed)])
    print("\n")
}
