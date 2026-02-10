// test array/list concatenation with ++ operator
pn main() {
    // test 1: basic int array concat (ArrayInt ++ ArrayInt)
    var a = [1, 2, 3]
    var b = [4, 5, 6]
    var c = a ++ b
    print("T1:" ++ string(c))

    // test 2: length of concatenated array
    print(" T2:" ++ string(len(c)))

    // test 3: empty array concat (Array ++ ArrayInt)
    var d = [] ++ [1, 2]
    print(" T3:" ++ string(d))

    // test 4: concat with empty (ArrayInt ++ Array)
    var e = [1, 2] ++ []
    print(" T4:" ++ string(e))

    // test 5: nested array concat (Array ++ Array)
    var f = [[1, 2], [3]] ++ [[4, 5]]
    print(" T5:" ++ string(f))
    print(" T6:" ++ string(len(f)))

    // test 7: string array concat (Array ++ Array)
    var g = ["hello", "world"] ++ ["foo", "bar"]
    print(" T7:" ++ string(g))
    print(" T8:" ++ string(len(g)))

    // test 9: chained concat
    var h = [1] ++ [2] ++ [3]
    print(" T9:" ++ string(h))

    // test 10: float array concat (ArrayFloat ++ ArrayFloat)
    var i = [1.5, 2.5] ++ [3.5]
    print(" T10:" ++ string(i))

    // test 11: mixed int and string (ArrayInt ++ Array → Array)
    var j = [1, 2] ++ ["a", "b"]
    print(" T11:" ++ string(j))

    // test 12: single element arrays
    var k = [42] ++ [99]
    print(" T12:" ++ string(k))

    // test 13: access elements of concatenated array
    var m = [10, 20] ++ [30, 40]
    print(" T13:" ++ string(m[0]))
    print(" T14:" ++ string(m[2]))
    print(" T15:" ++ string(m[3]))
}
