// Test: Mutation Limits
// Layer: 3 | Category: boundary | Covers: rapid type changes, many mutations, grow/shrink
// Mode: procedural

pn main() {
    // ===== Many sequential assignments =====
    var x = 0
    var i = 0
    while (i < 100) {
        x = x + 1
        i = i + 1
    }
    print(x)

    // ===== Array grow =====
    var arr = []
    var j = 0
    while (j < 50) {
        arr = [*arr, j]
        j = j + 1
    }
    print(len(arr))
    print(arr[0])
    print(arr[49])

    // ===== Map field additions =====
    var obj = {}
    obj.a = 1
    obj.b = 2
    obj.c = 3
    obj.d = 4
    obj.e = 5
    print(obj.a)
    print(obj.e)

    // ===== Overwrite same field many times =====
    var counter = {count: 0}
    var k = 0
    while (k < 100) {
        counter.count = k
        k = k + 1
    }
    print(counter.count)

    // ===== Nested mutation =====
    var data = {level1: {level2: {value: 0}}}
    data.level1.level2.value = 42
    print(data.level1.level2.value)

    // ===== Array element mutation =====
    var nums = [1, 2, 3, 4, 5]
    var m = 0
    while (m < 5) {
        nums[m] = nums[m] * 10
        m = m + 1
    }
    print(nums)

    // ===== Swap pattern in loop =====
    var a = 1
    var b = 2
    var n = 0
    while (n < 10) {
        var temp = a
        a = b
        b = temp + b
        n = n + 1
    }
    print(a)
    print(b)
}
