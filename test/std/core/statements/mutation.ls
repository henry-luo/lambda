// Test: Mutation
// Layer: 2 | Category: statement | Covers: array mutation, map mutation, element mutation
// Mode: procedural

pn main() {
    // ===== Array element assignment =====
    var arr = [1, 2, 3, 4, 5]
    arr[0] = 10
    arr[4] = 50
    print(arr)

    // ===== Array append =====
    var list = [1, 2, 3]
    list = [*list, 4]
    print(list)

    // ===== Map field assignment =====
    var obj = {name: "Alice", age: 30}
    obj.age = 31
    print(obj.name)
    print(obj.age)

    // ===== Map add new field =====
    var config = {host: "localhost"}
    config.port = 8080
    print(config.host)
    print(config.port)

    // ===== Nested map mutation =====
    var data = {user: {name: "Bob", scores: [90, 85, 92]}}
    data.user.name = "Robert"
    print(data.user.name)

    // ===== Swap values =====
    var p = 1
    var q = 2
    var temp = p
    p = q
    q = temp
    print(p)
    print(q)

    // ===== Accumulate into array =====
    var result = []
    var i = 0
    while (i < 5) {
        result = [*result, i * i]
        i = i + 1
    }
    print(result)

    // ===== Counter pattern =====
    var count = 0
    var idx = 0
    while (idx < 10) {
        if (idx % 3 == 0) count = count + 1
        idx = idx + 1
    }
    print(count)

    // ===== String building =====
    var text = ""
    var w = 0
    while (w < 3) {
        text = text & str(w)
        w = w + 1
    }
    print(text)
}
