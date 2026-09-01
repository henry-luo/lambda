// Dynamic dispatch returns an Item; the call descriptor must reopen string.
fn echo(value: string) string { value }

let indirect = echo
len(indirect("descriptor"))
