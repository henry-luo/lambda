// lambda counterpart of test/py/test_py_basic.py.
fn add(a, b) => a + b

pn main() {
    let x = 42
    let y = 3.14
    let a = 10 + 20
    let b = 7 * 6
    let name = "Lambda"
    let greeting = "Hello, " ++ name ++ "!"
    let flag = true
    let nums = [1, 2, 3, 4, 5]
    var total = 0

    for (i in 0 to 4) {
        total = total + i
    }

    print("Hello from Python!\n")
    print(string(x) ++ "\n")
    print(string(y) ++ "\n")
    print(string(a) ++ "\n")
    print(string(b) ++ "\n")
    print(greeting ++ "\n")
    print((if (flag) "True" else "False") ++ "\n")
    print(string(len(nums)) ++ "\n")
    print((if (x > 10) "x is big" else "x is small") ++ "\n")
    print(string(total) ++ "\n")
    print(string(add(3, 4)) ++ "\n")
}
