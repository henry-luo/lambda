// Test pipe-to-file operators |> and |>>
// These operators are only allowed in procedural code (pn functions)

pn main() {
    // Test 1: |> with map data (formatted as Lambda/Mark)
    let data = {name: "Lambda", version: 1, features: ["pipe", "file"]};
    let result1 = data |> "/tmp/lambda_test_pipe.mk";
    print("Test 1 - write map: ");
    print(result1);
    print("\n");
    
    // Test 2: |>> (append to file)  
    let more_data = {timestamp: t'2026-02-06', count: 42};
    let result2 = more_data |>> "/tmp/lambda_test_pipe.mk";
    print("Test 2 - append map: ");
    print(result2);
    print("\n");
    
    // Verify by reading back
    let content = input("/tmp/lambda_test_pipe.mk", 'text');
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
    simple |> 'lambda_test_symbol.mk';
    let symbol_content = input("./lambda_test_symbol.mk", 'text');
    print("Symbol target content:\n");
    print(symbol_content);
    
    // Test 5: scalar formatted as Mark
    42 |> "/tmp/lambda_test_scalar.mk";
    let scalar_content = input("/tmp/lambda_test_scalar.mk", 'text');
    print("Scalar file content:\n");
    print(scalar_content);
    
    // Test 6: output() with 4 params - write mode
    let data6 = {mode: "write", test: 6};
    let result6 = output(data6, "/tmp/lambda_test_output4.mk", null, "write");
    print("Test 6 - output4 write: ");
    print(result6);
    print("\n");
    let content6 = input("/tmp/lambda_test_output4.mk", 'text');
    print("Output4 write content:\n");
    print(content6);
    
    // Test 7: output() with 4 params - append mode
    let data7 = {mode: "append", test: 7};
    let result7 = output(data7, "/tmp/lambda_test_output4.mk", null, "append");
    print("Test 7 - output4 append: ");
    print(result7);
    print("\n");
    let content7 = input("/tmp/lambda_test_output4.mk", 'text');
    print("Output4 after append:\n");
    print(content7);
    
    // Test 8: output() 4 params - overwrite with write mode
    let data8 = {mode: "overwrite", test: 8};
    output(data8, "/tmp/lambda_test_output4.mk", null, "write");
    let content8 = input("/tmp/lambda_test_output4.mk", 'text');
    print("Output4 after overwrite:\n");
    print(content8);
    
    // Clean up symbol target file
    fs.delete('lambda_test_symbol.mk');
    
    print("done");
}
