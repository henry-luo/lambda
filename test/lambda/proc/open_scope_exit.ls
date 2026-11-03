// S12.4.4v4: a successful transfer crossing open commits at the boundary.
pn early() any^ {
    open target = temp.'open_early_return' {
        put target.n = 7
        return {text:"returned",count:12}
    }
    return null
}
pn early_empty() {
    open target = temp.'open_early_empty' {
        put target.n = 8
        return
    }
}
pn wrong_return(value) int^ {
    open target = temp.'open_return_type' {
        put target.n = 7
        return value
    }
    return 0
}
pn nullable_bare_return() int?^ {
    open target = temp.'open_bare_optional' { put target.n = 7; return }
    return 0
}
pn bad_bare_return() int^ {
    open target = temp.'open_bare_type' { put target.n = 7; return }
    return 0
}
pn convert_return(value) float^ {
    open target = temp.'open_return_conversion' {
        put target.n = 7
        return value
    }
    return 0.0
}
pn commit_failure() any^ {
    open target = temp.'open_commit_failure' {
        del target.n; del target.n
        return 42
    }
    return null
}
pn broken_break() any^ {
    while (true) {
        open target = temp.'open_break_commit_failure' { del target.n; del target.n; break }
    }
    return 3
}
pn recursive_open(n) any^ {
    if (n == 0) { return 42 }
    open target = temp.'open_recursive' {
        put target.n = n
        return recursive_open(n - 1)
    }
    return null
}
pn main() {
    let a = temp('open_early_return', {n:1})^
    let value = early()^
    print(value.text == "returned" and value.count == 12); print("\n")
    print(temp.'open_early_return'#n == 7); print("\n")
    open target = temp.'open_early_return' { put target.n = 2 }
    print(temp.'open_early_return'#n == 2); print("\n")
    let b = temp('open_early_empty', {n:1})^
    early_empty()
    print(temp.'open_early_empty'#n == 8); print("\n")
    let c = temp('open_early_break', {n:1})^
    while (true) {
        open target = temp.'open_early_break' { put target.n = 3; break }
    }
    print(temp.'open_early_break'#n == 3); print("\n")
    open target = temp.'open_early_break' { put target.n = 4 }
    print(temp.'open_early_break'#n == 4); print("\n")
    let d = temp('open_early_continue', {n:1})^
    var i = 0
    while (i < 2) {
        i = i + 1
        open target = temp.'open_early_continue' { put target.n = i; continue }
    }
    print(temp.'open_early_continue'#n == 2); print("\n")
    let e = temp('open_stay', {n:1})^
    open target = temp.'open_stay' {
        var j = 0
        while (j < 2) { j = j + 1; if (j == 1) { continue }; break }
        put target.n = j
    }
    print(temp.'open_stay'#n == 2); print("\n")
    let f = temp('open_return_type', {n:1})^
    var caught_type = false
    let bad = wrong_return("bad") ^ { caught_type = true; null }
    print(caught_type and temp.'open_return_type'#n == 1); print("\n")
    open target = temp.'open_return_type' { put target.n = 2 }
    print(temp.'open_return_type'#n == 2); print("\n")
    let optional = temp('open_bare_optional', {n:1})^
    print(nullable_bare_return()^ == null and temp.'open_bare_optional'#n == 7); print("\n")
    let bare = temp('open_bare_type', {n:1})^
    var caught_bare = false
    let no_value = bad_bare_return() ^ { caught_bare = true; null }
    print(caught_bare and temp.'open_bare_type'#n == 1); print("\n")
    let conversion = temp('open_return_conversion', {n:1})^
    let converted = convert_return(12)^
    print(converted == 12.0 and temp.'open_return_conversion'#n == 7); print("\n")
    let g = temp('open_commit_failure', {n:1})^
    var caught_commit = false
    let rejected = commit_failure() ^ { caught_commit = true; null }
    print(caught_commit and temp.'open_commit_failure'#n == 1); print("\n")
    open target = temp.'open_commit_failure' { put target.n = 2 }
    print(temp.'open_commit_failure'#n == 2); print("\n")
    let broken = temp('open_break_commit_failure', {n:1})^
    var caught_break = false
    let broken_result = broken_break() ^ { caught_break = true; null }
    print(caught_break and temp.'open_break_commit_failure'#n == 1); print("\n")
    let h = temp('open_recursive', {n:1})^
    print(recursive_open(1)^ == 42 and temp.'open_recursive'#n == 1); print("\n")
    var nested_error = false
    let ignored = recursive_open(2) ^ { nested_error = true; null }
    print(nested_error and temp.'open_recursive'#n == 1)
}
