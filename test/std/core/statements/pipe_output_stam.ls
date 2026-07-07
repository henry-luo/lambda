// Test: Explicit Output
// Layer: 2 | Category: statement | Covers: output() write and append to file
// Mode: procedural

pn main() {
    // ===== Output to file (write) =====
    output("Hello, File!", "./temp/pipe_test_output.txt")^

    // ===== Read back the file =====
    let content = read("./temp/pipe_test_output.txt")
    print(content)

    // ===== Output append =====
    output("\nSecond line", "./temp/pipe_test_output.txt", {mode: "append"})^
    let content2 = read("./temp/pipe_test_output.txt")
    print(content2)

    // ===== Output collection to file =====
    output([1, 2, 3] |> join(", "), "./temp/pipe_list_output.txt")^
    let list_content = read("./temp/pipe_list_output.txt")
    print(list_content)

    // ===== Pipe transformed data =====
    output([1, 2, 3, 4, 5]
        |> map((x) => x * x)
        |> join(" "),
        "./temp/pipe_transform_output.txt")^
    let transform_content = read("./temp/pipe_transform_output.txt")
    print(transform_content)
}
