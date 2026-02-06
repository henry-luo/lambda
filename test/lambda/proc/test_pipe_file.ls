// Test pipe-to-file operators |> and |>>
// These operators are only allowed in procedural code (pn functions)

pn main() {
    // Test 1: |> with map data (formatted as Lambda/Mark)
    let data = {name: "Lambda", version: 1, features: ["pipe", "file"]};
    let result1 = data |> "/tmp/lambda_test_pipe.txt";
    print("Test 1 - write map: ");
    print(result1);
    print("\n");
    
    // Test 2: |>> (append to file)  
    let more_data = {timestamp: t'2026-02-06', count: 42};
    let result2 = more_data |>> "/tmp/lambda_test_pipe.txt";
    print("Test 2 - append map: ");
    print(result2);
    print("\n");
    
    // Verify by reading back
    let content = input("/tmp/lambda_test_pipe.txt", 'text');
    print("Pipe file content:\n");
    print(content);
    
    // Test 3: string data (output as text, no Mark formatting)
    let text = "Hello, world!";
    text |> "/tmp/lambda_test_text.txt";
    let text_content = input("/tmp/lambda_test_text.txt", 'text');
    print("Text file content:\n");
    print(text_content);
    print("\n");
    
    // Test 4: with symbol as target filename (write to cwd)
    let simple = {test: "symbol target"};
    simple |> 'lambda_test_symbol.txt';
    let symbol_content = input("./lambda_test_symbol.txt", 'text');
    print("Symbol target content:\n");
    print(symbol_content);
    
    // Test 5: scalar formatted as Mark
    42 |> "/tmp/lambda_test_scalar.txt";
    let scalar_content = input("/tmp/lambda_test_scalar.txt", 'text');
    print("Scalar file content:\n");
    print(scalar_content);
    
    // Clean up symbol target file
    fs.delete('lambda_test_symbol.txt');
    
    print("done");
}
