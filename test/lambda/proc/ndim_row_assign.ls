// S1.6: an N-D array is invisibly the sequence of its leading-axis rows, so
// `m[i] = v` replaces row i. The open write path used to widen the flat
// leaves, turning [[1, 2], [1, 2]] into [[9, 2], 2, 1, 2] on `m[0] = [9, 2]`.
pn main() {
    var same = [[1, 2], [1, 2]]
    same[0] = [9, 2]
    print("same shape: " ++ format(same, 'json') ++ " shape " ++ format(shape(same), 'json') ++ "\n")
    var longer = [[1, 2], [1, 2]]
    longer[1] = [7, 8, 9]
    print("longer row: " ++ format(longer, 'json') ++ "\n")
    var scalar = [[1, 2], [3, 4]]
    scalar[0] = "s"
    print("scalar: " ++ format(scalar, 'json') ++ "\n")
    var rank3 = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
    rank3[1] = [0]
    print("rank 3: " ++ format(rank3, 'json') ++ "\n")
    var widened = [[1, 2], [3, 4]]
    widened[0] = [1.5, 2]
    print("float row: " ++ format(widened, 'json') ++ "\n")
    var copied = [[1, 2], [3, 4]]
    copied[1] = copied[0]
    copied[0][0] = 99
    print("row copy: " ++ format(copied, 'json') ++ "\n")
    var spliced = [[1, 2], [3, 4]]
    spliced[1] = (5, 6)
    print("list splice: " ++ format(spliced, 'json') ++ "\n")
    var built: array = []
    built.push([1, 2])
    built.push([1, 2])
    var through = [for (x in built) x]
    through[0] = [9, 2]
    print("for copy: " ++ format(through, 'json') ++ "\n")
}
