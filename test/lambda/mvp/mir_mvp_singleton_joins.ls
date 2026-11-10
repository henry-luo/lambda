// S10.6.1/S9.1.2: erased singleton joins retain fresh results and canonical fallback.
fn appended(left: any, member: any) any => left ++ [member]
pn main() {
    var ints = fill(2, 3)^
    let grown = appended(ints, 7)
    ints[0] = 9
    print([ints, grown]); print("\n")
    print(appended(fill(2, 1.5)^, -2.5)); print("\n")
    print(appended(fill(2, true)^, false)); print("\n")
    print(appended([1, 2], "text")); print("\n")
    print(appended("text", 5)); print("\n")
    print(appended(null, [2, 3])); print("\n")
    print(appended([1, 2], [3, 4])); print("\n")
    print(appended({value: 1}, 2) is error); print("\n")
    print(appended(error("left"), 2).message); print("\n")
    let with_error = appended([1], error("member"))
    print([len(with_error), with_error[1].message]); print("\n")
}
