// A nullable bool keeps its 0/1/2 lane through locals and condition evaluation.
fn pass_bool(value: bool?) bool? => value

pn main() {
    let enabled: bool? = pass_bool(true)
    let absent: bool? = pass_bool(null)
    if (enabled) {
        print("enabled")
    }
    print(" ")
    if (absent) {
        print("unexpected")
    } else {
        print("absent")
    }
    print(" ")
    print(not absent)
    print(" ")
    print([enabled, absent])
    print("\n")
}
