// Test: Explicit Output
// Layer: 2 | Category: statement | Covers: output() write and append to file
// Mode: procedural

pn main() {
    // ===== Output to file (write) =====
    output("Hello, File!", "./temp/pipe_test_output.txt")^

    // ===== Read back the file =====
    let content = input("./temp/pipe_test_output.txt", 'text')^
    print(content)

    // ===== Output append =====
    output("\nSecond line", "./temp/pipe_test_output.txt", {mode: "append"})^
    let content2 = input("./temp/pipe_test_output.txt", 'text')^
    print(content2)

    // ===== Output collection to file =====
    output([1, 2, 3] |> string(~) |> join(", "), "./temp/pipe_list_output.txt")^
    let list_content = input("./temp/pipe_list_output.txt", 'text')^
    print(list_content)

    // ===== Pipe transformed data =====
    output([1, 2, 3, 4, 5]
        |> ~ * ~ |> string(~)
        |> join(" "),
        "./temp/pipe_transform_output.txt")^
    let transform_content = input("./temp/pipe_transform_output.txt", 'text')^
    print(transform_content)
}
