// S9.1.2 / D4.4.1: MIR Direct picks a raw or share-checked store at compile
// time from its "may be shared" facts, and used to update those facts in
// emission order only. A detach or rebind inside one branch arm cleared the
// fact for the path that skipped the arm, and a share made late in a loop
// body never reached the stores emitted earlier in the body; each case below
// leaked a write into a value another binding still held. T0 was correct.
type Box = {items: array}
type Arr = {l0: array}
type Holder = {arr: Arr}

pn fresh() array { return [0, 0, 0] }
pn null2() array { return [null, null] }

pn arm_detach(flag: bool) {
    var b: Box = {items: fresh()}
    var c = b.items
    var keep = c
    if (flag) { c[0] = 5 }
    c[1] = 7
    print("arm_detach " ++ flag ++ ": c=" ++ c ++ " keep=" ++ keep ++ "\n")
}

pn arm_rebind(flag: bool) {
    var src = fresh()
    var c = src
    if (flag) { c = fresh() }
    c[2] = 9
    print("arm_rebind " ++ flag ++ ": c=" ++ c ++ " src=" ++ src ++ "\n")
}

pn else_after_rebind(flag: bool) {
    var src = fresh()
    var c = src
    if (flag) { c = fresh() } else { c[0] = 4 }
    print("else_after_rebind " ++ flag ++ ": c=" ++ c ++ " src=" ++ src ++ "\n")
}

pn match_rebind(k: int) {
    var src = fresh()
    var c = src
    match k {
        case 1 { c = fresh() }
        default { c[1] = 1 }
    }
    c[0] = 8
    print("match_rebind " ++ k ++ ": c=" ++ c ++ " src=" ++ src ++ "\n")
}

pn while_share() {
    var c = fresh()
    var snaps = []
    var i = 0
    while (i < 3) {
        c[0] = i
        var d = c
        push(snaps, d)
        i = i + 1
    }
    print("while_share: " ++ snaps ++ "\n")
}

pn for_share() {
    var c = fresh()
    var snaps = []
    for i in 0 to 2 {
        c[1] = i
        push(snaps, c)
    }
    print("for_share: " ++ snaps ++ "\n")
}

pn break_share() {
    var c = fresh()
    var keep = []
    var i = 0
    while (true) {
        c[2] = i
        if (i == 1) {
            keep = c
            break
        }
        i = i + 1
    }
    c[2] = 99
    print("break_share: c=" ++ c ++ " keep=" ++ keep ++ "\n")
}

pn nested_share() {
    var c = fresh()
    var rows = []
    var i = 0
    while (i < 2) {
        var j = 0
        while (j < 2) {
            c[0] = i * 10 + j
            j = j + 1
        }
        push(rows, {row: c})
        i = i + 1
    }
    print("nested_share: " ++ rows ++ "\n")
}

// the havlak2 three-level store shape: a rebind in one arm of a nested
// handle must not unmark the arm-skipping path
pn arr_set(var a: Arr, i0: int, i1: int, val: any) int {
    var l0 = a.l0
    var c1 = l0[i0]
    if (c1 == null) {
        var _d = 0
        c1 = null2()
    }
    c1[i1] = val
    l0[i0] = c1
    a.l0 = l0
    return 0
}

pn nested_handle() {
    var a: Arr = {l0: null2()}
    arr_set(a, 0, 0, 1)
    var h: Holder = {arr: a}
    var b: Arr = h.arr
    arr_set(a, 0, 1, 2)
    print("nested_handle: a=" ++ a.l0 ++ " b=" ++ b.l0 ++ "\n")
    var holder: Holder = {arr: a}
    var a2: Arr = holder.arr
    arr_set(a2, 0, 0, 9)
    print("nested_handle: a2=" ++ a2.l0 ++ " holder=" ++ holder.arr.l0 ++ "\n")
}

pn main() {
    arm_detach(true)
    arm_detach(false)
    arm_rebind(true)
    arm_rebind(false)
    else_after_rebind(true)
    else_after_rebind(false)
    match_rebind(1)
    match_rebind(2)
    while_share()
    for_share()
    break_share()
    nested_share()
    nested_handle()
}
