// D2.6.2/D6.3: packed fields retain their live descriptor and precise owner.
fn pair(left: any, right: any) any => {left: left, right: right}
fn left_of(record: any) any => record.left
fn tree(depth: int) any {
    if (depth == 0) { return {left: null, right: null} }
    return pair(tree(depth - 1), tree(depth - 1))
}
fn leaves(node: any) any {
    if (node.left == null) { return 1 }
    return leaves(node.left) + leaves(node.right)
}
// S5.1.1v2: literal comparisons preserve Error and cross-family results.
fn literal_equal(value: any) any =>
    [value == "text", "text" == value, value != "text", value == "", value == "téxt"]
fn literal_error(value: any) any => (value != "text") ^ { ^.message }
fn character(source: any, first: any, limit: any) any => slice(source, first, limit)
fn erased_equal(left: any, right: any) any => [left == right, left != right]
fn split_word(source: any, key: int) any { let words = split(source, " "); return words[key] }
fn split_parts(source: any, sections: any) any => split(source, sections)
pn main() {
    let node = tree(6)
    print([leaves(node), node.left.right.left != null]); print("\n")
    let wide = pair(9223372036854775807i64, 1.25)
    let saved = left_of(wide)
    let churn = tree(5)
    print([saved == 9223372036854775807i64, wide.right == 1.25, leaves(churn)]); print("\n")
    let problem: any = error({code: 7005, message: "kept"})
    let record = pair(problem, [1, 2])
    print([record.left.message, record.right[1], literal_error(problem)]); print("\n")
    print(literal_equal("text")); print("\n")
    print(literal_equal(null)); print("\n")
    print(literal_equal('text')); print("\n")
    print(literal_equal("téxt")); print("\n")
    print([character("abc", 1, 2), character("abc", -1, 0),
        character("abc", 3, 4), character("éλ", 1, 2),
        character("abc", 1.0, 2.0), character(null, 0, 1),
        character([1, 2, 3], 1, 2), character("abc", 1, 3),
        character("abc", null, 1)]); print("\n")
    print([erased_equal("éλ", "éλ"), erased_equal("éλ", "éβ"),
        erased_equal("word", 'word'), erased_equal(true, true),
        erased_equal(false, 0), erased_equal([1, 2], [1, 2])]); print("\n")
    let failures = erased_equal(problem, true)
    print([failures[0].message, failures[1].message]); print("\n")
    // S17.1.1/S8.2: a text separator excludes the numeric split overload.
    print([split_word("one two", 1), split_word(null, 0),
        split_word([1, 2], 0) is error, split_word(problem, 0).message,
        split_parts([1, 2, 3, 4], 2)[0] == [1, 2]]); print("\n")
}
