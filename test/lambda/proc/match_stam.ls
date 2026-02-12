// Match statement tests (procedural form with pn/main)

pn main() {
    // Test 1: match statement with braced arms
    var result = ""
    match 42 {
        case int {
            result = "integer"
        }
        case string {
            result = "text"
        }
    }
    print(result)

    // Test 2: match statement with expression arms
    match "hello"
        case int: print("is int")
        case string: print("is string")
        case bool: print("is bool")
        default: print("is other")

    // Test 3: match with literal values
    var code = 404
    match code
        case 200: print("OK")
        case 404: print("Not Found")
        case 500: print("Server Error")
        default: print("Unknown")

    // Test 4: match statement with current item ~
    match 42
        case 0: print("zero")
        case int: print(if (~ > 0) "positive" else "negative")

    // Test 5: match as expression in var assignment
    var x = match 100 case 0: "zero" default: "nonzero"
    print(x)
}
