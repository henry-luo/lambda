// Test procedural closure capture mutation
// Tests BUG-7 fix: closures can now assign to captured var variables
// Semantics: capture-by-value with writable copy in env struct
// Named inner functions with captures are referenced as values (let f = name)
// then called through the variable (f()) to use fn_call dispatch with env.

// Test 1: basic counter pattern — closure increments its own copy
pn test_counter() {
    var count = 0
    pn inc() {
        count = count + 1
        count
    }
    let f = inc
    let a = f()
    let b = f()
    let c = f()
    a + b + c  // 1 + 2 + 3 = 6
}

// Test 2: outer var is independent of closure's copy
pn test_outer_independent() {
    var x = 10
    pn set_x() {
        x = 99
        x
    }
    let f = set_x
    let inner = f()  // closure sets its copy to 99
    x + inner  // outer x still 10, inner 99 => 109
}

// Test 3: multiple mutable captures
pn test_multi_capture() {
    var a = 1
    var b = 2
    pn swap_add() {
        let tmp = a
        a = b
        b = tmp
        a + b  // after swap: a=2, b=1 => 3
    }
    let f = swap_add
    f()
}

// Test 4: closure with string concatenation on captured var
pn test_string_concat() {
    var acc = ""
    pn append(s) {
        acc = acc ++ s
        acc
    }
    let f = append
    let r1 = f("hello")
    let r2 = f(" world")
    r2  // "hello world"
}

// Test 5: read-only capture still works (no mutation)
pn test_read_only_capture() {
    var x = 42
    fn get_x(dummy) => x
    let f = get_x
    f(0)  // 42
}

// Test 6: closure called multiple times accumulates state
pn test_accumulator() {
    var total = 0
    pn add(n) {
        total = total + n
        total
    }
    let f = add
    let r1 = f(10)
    let r2 = f(20)
    let r3 = f(5)
    r3  // 10 + 20 + 5 = 35
}

// Test 7: two independent closures sharing same outer var
pn test_two_closures() {
    var x = 0
    pn inc_x() {
        x = x + 1
        x
    }
    pn inc_x2() {
        x = x + 10
        x
    }
    let f1 = inc_x
    let f2 = inc_x2
    let a = f1()   // f1's copy: 0+1 = 1
    let b = f2()   // f2's copy: 0+10 = 10 (independent)
    a + b  // 1 + 10 = 11
}

// Test 8: captured var with type change (widened in closure)
pn test_capture_type_widen() {
    var val = 42
    pn set_string() {
        val = "hello"
        val
    }
    let f = set_string
    f()  // "hello"
}

pn main() {
    print("T1:")
    print(test_counter())
    print(" T2:")
    print(test_outer_independent())
    print(" T3:")
    print(test_multi_capture())
    print(" T4:")
    print(test_string_concat())
    print(" T5:")
    print(test_read_only_capture())
    print(" T6:")
    print(test_accumulator())
    print(" T7:")
    print(test_two_closures())
    print(" T8:")
    print(test_capture_type_widen())
    "done"
}
