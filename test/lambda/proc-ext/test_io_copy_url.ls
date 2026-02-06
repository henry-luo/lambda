// Test io.copy with remote URL source
// Run with: ./lambda.exe run test/lambda/proc/test_io_copy_url.ls

pn main() {
    print("Testing io.copy with remote URL...")
    
    // Setup: ensure test output directory exists
    io.mkdir("./test_output")
    
    // Test 1: Copy from a remote URL to a local file
    print("1. Testing io.copy(url, local_file)...")
    
    // Copy from URL to local file (httpbin returns JSON)
    io.copy("https://httpbin.org/json", "./test_output/copied_from_url.json")
    print("   Copied from: https://httpbin.org/json")
    print("   To: ./test_output/copied_from_url.json")
    
    // Verify the file exists
    if exists("./test_output/copied_from_url.json") {
        print("   File exists: true")
    } else {
        print("   ERROR: File was not created!")
    }
    
    // Test 2: io.fetch via io module
    print("2. Testing io.fetch...")
    let result = io.fetch("https://httpbin.org/get")
    if result != null {
        print("   Fetch returned data: true")
    } else {
        print("   ERROR: Fetch returned null!")
    }
    
    // Cleanup
    print("3. Cleaning up...")
    io.delete("./test_output/copied_from_url.json")
    print("   Deleted: ./test_output/copied_from_url.json")
    
    print("io.copy URL tests completed!")
}
