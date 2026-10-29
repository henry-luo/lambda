type OpenRecord = {}
type OptionalValue = {opt?: int}
let bad: OpenRecord = {opt:"bad"}
fn read_optional(r: OptionalValue) => r.opt
read_optional(bad)
