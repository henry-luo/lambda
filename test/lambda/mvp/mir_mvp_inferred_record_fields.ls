// D4.3.1-D4.3.3: inferred field layouts cannot add array element contracts.
fn make_record() => {labels: ["one", "two"], values: [3, 4]}
fn checked(value: any) any {
    let row: {numbers: int[]} = value
    row
}
let table = {letters: ["a", "b"], integers: [1, 2], flags: [true, false],
    decimals: [1.5, 2.5], mixed: [1, "two"], rows: [["x", "y"], ["z"]]}
let made = make_record()
let admitted = checked({numbers: [5, 6]})
let rejected = checked({numbers: ["bad"]});
[
    table.letters[1] == "b", table.integers[1] == 2,
    table.flags[1] == false, table.decimals[1] == 2.5,
    table.mixed[1] == "two", table.rows[1][0] == "z",
    made.labels[1] == "two", made.values[1] == 4,
    admitted.numbers[1] == 6, rejected is error
]
