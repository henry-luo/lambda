// S16.8.9: computed literal keys stay in the NameKey domain.
fn non_name() any { 1 }

pn main() {
    let invalid = {[non_name()]: "one"}
    print(invalid)
}
