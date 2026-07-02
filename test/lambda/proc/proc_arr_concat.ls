// test array/list concatenation with ++ operator
pn main() {
    // test 1: basic int array concat (ArrayInt ++ ArrayInt)
    var a = [1, 2, 3]
    var b = [4, 5, 6]
    var c = a ++ b
    print("T1:" ++ (c))

    // test 2: length of concatenated array
    print(" T2:" ++ (len(c)))

    // test 3: empty array concat (Array ++ ArrayInt)
    var d = [] ++ [1, 2]
    print(" T3:" ++ (d))

    // test 4: concat with empty (ArrayInt ++ Array)
    var e = [1, 2] ++ []
    print(" T4:" ++ (e))

    // test 5: nested array concat (Array ++ Array)
    var f = [[1, 2], [3]] ++ [[4, 5]]
    print(" T5:" ++ (f))
    print(" T6:" ++ (len(f)))

    // test 7: string array concat (Array ++ Array)
    var g = ["hello", "world"] ++ ["foo", "bar"]
    print(" T7:" ++ (g))
    print(" T8:" ++ (len(g)))

    // test 9: chained concat
    var h = [1] ++ [2] ++ [3]
    print(" T9:" ++ (h))

    // test 10: float array concat (ArrayFloat ++ ArrayFloat)
    var i = [1.5, 2.5] ++ [3.5]
    print(" T10:" ++ (i))

    // test 11: mixed int and string (ArrayInt ++ Array → Array)
    var j = [1, 2] ++ ["a", "b"]
    print(" T11:" ++ (j))

    // test 12: single element arrays
    var k = [42] ++ [99]
    print(" T12:" ++ (k))

    // test 13: access elements of concatenated array
    var m = [10, 20] ++ [30, 40]
    print(" T13:" ++ (m[0]))
    print(" T14:" ++ (m[2]))
    print(" T15:" ++ (m[3]))
}
