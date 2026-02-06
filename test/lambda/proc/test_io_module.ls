// Test io module functions
// Run with: ./lambda.exe run test/lambda/proc/test_io_module.ls

// io is a built-in module, accessed via io.method() syntax

pn main() {
    print("Testing io module functions...")
    
    // Test 1: io.mkdir - create a directory
    print("1. Testing io.mkdir...")
    io.mkdir("./test_output/io_test_dir")
    print("   Created directory: ./test_output/io_test_dir")
    
    // Test 2: exists - check if exists (global function)
    print("2. Testing exists...")
    if exists("./test_output/io_test_dir") {
        print("   Directory exists: true")
    }
    
    // Test 3: io.touch - create a file
    print("3. Testing io.touch...")
    io.touch("./test_output/io_test_dir/test.txt")
    print("   Created file: ./test_output/io_test_dir/test.txt")
    
    // Test 4: io.copy - copy the file
    print("4. Testing io.copy...")
    io.copy("./test_output/io_test_dir/test.txt", "./test_output/io_test_dir/test_copy.txt")
    print("   Copied to: ./test_output/io_test_dir/test_copy.txt")
    
    // Test 5: io.move - rename/move a file
    print("5. Testing io.move...")
    io.move("./test_output/io_test_dir/test_copy.txt", "./test_output/io_test_dir/test_moved.txt")
    print("   Moved to: ./test_output/io_test_dir/test_moved.txt")
    
    // Test 6: io.rename - rename a file
    print("6. Testing io.rename...")
    io.rename("./test_output/io_test_dir/test_moved.txt", "./test_output/io_test_dir/test_renamed.txt")
    print("   Renamed to: ./test_output/io_test_dir/test_renamed.txt")
    
    // Test 7: io.delete - delete files
    print("7. Testing io.delete...")
    io.delete("./test_output/io_test_dir/test_renamed.txt")
    io.delete("./test_output/io_test_dir/test.txt")
    print("   Deleted files")
    
    // Test 8: io.chmod - change permissions (Unix only)
    print("8. Testing io.chmod...")
    io.touch("./test_output/io_test_dir/chmod_test.txt")
    io.chmod("./test_output/io_test_dir/chmod_test.txt", "644")
    print("   Changed permissions to 644")
    
    // Cleanup
    print("9. Cleaning up...")
    io.delete("./test_output/io_test_dir")  // io.delete handles directories recursively
    print("   Cleaned up test directory")
    
    // Verify cleanup
    if exists("./test_output/io_test_dir") {
        print("   WARNING: Directory still exists!")
    } else {
        print("   Directory removed successfully")
    }
    
    print("All io module tests completed!")
}
