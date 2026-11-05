// S10.1.5v3: the proviso tests one rooted subject and rejects to null.
pn source(var calls: int) any {
    calls = calls + 1
    return {items: [1, 2, 3], age: 30}
}
pn main() {
    var calls: int = 0
    let held = source(calls) that len(items) == 3 and age > 18
    print([calls == 1, held.age == 30, (5 that ~ > 9) is null,
        (5 that ~key == null) == 5]); print("\n")
    print([[1, 2, 3] that len(~) == 3, [1, 2, 3] that len(~) > 3]); print("\n")
    let absent: bool? = null
    print([(7 that absent) is null, (7 that error("predicate")) is null,
        (7 that (error("first") or true)) is null,
        (7 that (false and error("unused"))) is null]); print("\n")
    print([(7 that true) == 7, (7 that "yes") == 7,
        (7 that 0) == 7, (null that true) is null]); print("\n")
    let tree = {child: {n: 7}}
    print(match tree { case map: match ~.child {
        case map: [(9 that ~.~~ == tree) == 9, (9 that ~./ == tree) == 9,
            ~.n == 7, ~.~~ == tree, ~key is null]
        default: false
    } default: false }); print("\n")
    let tiny = 1e-310 that len(string([1, 2, 3])) > 0
    print([tiny == 1e-310, ((8 that ~ > 0) that ~ < 10) == 8]); print("\n")
}
