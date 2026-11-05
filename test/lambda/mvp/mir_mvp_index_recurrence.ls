// D8.2.5: index-carrier queries must share the active recurrence proof.
pn table_for(pattern: int[]) int[]^ {
    var table: int[] = fill(len(pattern), 0)^
    var length = 0
    var index = 1
    while (index < len(pattern)) {
        if (pattern[index] == pattern[length]) {
            length = length + 1
            table[index] = length
            index = index + 1
        } else if (length > 0) {
            length = table[length - 1]
        } else {
            index = index + 1
        }
    }
    table
}
pn main() {
    let table = table_for([1, 2, 1, 2, 3])^
    print(table)
    print("\n")
}
