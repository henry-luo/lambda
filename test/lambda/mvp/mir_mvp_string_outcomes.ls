// guarded appends retain deferred errors, operand effects and snapshots (S7.7.2v2, S10.6.1).
fn append_input(x: any) string^ {
    var result: string = "p"
    result = result ++ ("[" ++ x ++ "]")
    return result
}
fn checked_text(x: any) string^ { let admitted: string = x; return admitted }
pn touched(var marks: int[], x: any) any {
    marks[0] = marks[0] + 1
    return x
}
pn append_effects(x: any, var marks: int[]) any {
    var result: string = x
    result = result ++ (touched(marks, "a") ++ touched(marks, "b"))
    marks[0] = marks[0] + 10
    return [result is error, marks[0]]
}
pn snapshots(x: any) any {
    var result: string = "a"
    result = result ++ (x ++ "b")
    let saved = result
    result = result ++ ("c" ++ x)
    return [saved, result]
}
pn repeat_text(x: any) any {
    var result: string = ""
    var i: int = 0
    while (i < 4) { result = result ++ (x ++ "!"); i = i + 1 }
    return result
}
pn main() {
    print([append_input("ok"), append_input(7), append_input(null)]); print("\n")
    print([append_input([1, 2]) is error, append_input([true, false]) is error]); print("\n")
    print(snapshots("λ🙂")); print("\n")
    print(repeat_text("λ")); print("\n")
    var marks: int[] = [0]
    print(append_effects(7, marks)); print("\n")
    print(append_effects("p", marks)); print("\n")
    let invalid = checked_text(7)
    print([append_input(invalid) is error, repeat_text(invalid) is error]); print("\n")
}
