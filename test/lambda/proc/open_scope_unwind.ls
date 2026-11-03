// Open blocks release their transaction before errors reach an outer handler (S12.4.2, S12.4.4v4).
pn raise_inside() any^ {
    open target = temp.'open_matrix_raise' {
        put target.n = 7
        raise error("stop")
    }
    return null
}
pn failing() any^ { raise error("failure") }
pn propagate_inside() any^ {
    open target = temp.'open_matrix_propagate' {
        put target.n = 7
        failing()^
    }
    return null
}
pn return_error_inside() any^ {
    open target = temp.'open_matrix_return_error' {
        put target.n = 7
        return error("returned error")
    }
    return null
}
pn recover_inside() any^ {
    open target = temp.'open_matrix_recover' {
        failing() ^ { null }
        put target.n = 9
    }
    return null
}
pn recover_inner_block() {
    open target = temp.'open_matrix_inner_block' {
        put target.n = 2
        if (true) {
            var xs = [1]
            xs[3] = 9
        }
        put target.n = 3
    }
}
pn reject_nested() any^ {
    open outer = temp.'open_matrix_nested' {
        put outer.n = 7
        open inner = temp.'open_matrix_nested' { put inner.n = 8 }
    }
    return null
}
pn main() {
    let a = temp('open_matrix_raise', {n:1})^
    var message = ""
    raise_inside() ^ { message = ^.message }
    print(message == "stop"); print("\n")
    print(temp.'open_matrix_raise'#n == 1); print("\n")
    open target = temp.'open_matrix_raise' { put target.n = 2 }
    print(temp.'open_matrix_raise'#n == 2); print("\n")
    let b = temp('open_matrix_propagate', {n:1})^
    propagate_inside() ^ { null }
    print(temp.'open_matrix_propagate'#n == 1); print("\n")
    open target = temp.'open_matrix_propagate' { put target.n = 2 }
    print(temp.'open_matrix_propagate'#n == 2); print("\n")
    let returned = temp('open_matrix_return_error', {n:1})^
    return_error_inside() ^ { null }
    print(temp.'open_matrix_return_error'#n == 1); print("\n")
    open target = temp.'open_matrix_return_error' { put target.n = 2 }
    print(temp.'open_matrix_return_error'#n == 2); print("\n")
    let c = temp('open_matrix_recover', {n:1})^
    recover_inside()^
    print(temp.'open_matrix_recover'#n == 9); print("\n")
    let inner = temp('open_matrix_inner_block', {n:1})^
    recover_inner_block()
    print(temp.'open_matrix_inner_block'#n == 3); print("\n")
    let loop = temp('open_matrix_loop', {n:1})^
    open target = temp.'open_matrix_loop' {
        var i = 0
        while (i < 2) {
            i = i + 1
            if (i == 1) { continue }
            break
        }
        put target.n = i
    }
    print(temp.'open_matrix_loop'#n == 2); print("\n")
    let d = temp('open_matrix_nested', {n:1})^
    reject_nested() ^ { null }
    print(temp.'open_matrix_nested'#n == 1); print("\n")
    open target = temp.'open_matrix_nested' { put target.n = 2 }
    print(temp.'open_matrix_nested'#n == 2)
}
