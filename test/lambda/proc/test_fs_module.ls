// Test fs module functions
// Run with: ./lambda.exe run test/lambda/test_fs_module.ls

// fs is a built-in module, accessed via fs.method() syntax

pn main() {
    print("Testing fs module functions...")
    
    // Test 1: fs.mkdir - create a directory
    print("1. Testing fs.mkdir...")
    fs.mkdir("./test_output/fs_test_dir")
    print("   Created directory: ./test_output/fs_test_dir")
    
    // Test 2: fs.exists - check if exists
    print("2. Testing fs.exists...")
    if fs.exists("./test_output/fs_test_dir") {
        print("   Directory exists: true")
    }
    
    // Test 3: fs.touch - create a file
    print("3. Testing fs.touch...")
    fs.touch("./test_output/fs_test_dir/test.txt")
    print("   Created file: ./test_output/fs_test_dir/test.txt")
    
    // Test 4: fs.copy - copy the file
    print("4. Testing fs.copy...")
    fs.copy("./test_output/fs_test_dir/test.txt", "./test_output/fs_test_dir/test_copy.txt")
    print("   Copied to: ./test_output/fs_test_dir/test_copy.txt")
    
    // Test 5: fs.move - rename/move a file
    print("5. Testing fs.move...")
    fs.move("./test_output/fs_test_dir/test_copy.txt", "./test_output/fs_test_dir/test_moved.txt")
    print("   Moved to: ./test_output/fs_test_dir/test_moved.txt")
    
    // Test 6: fs.rename - rename a file
    print("6. Testing fs.rename...")
    fs.rename("./test_output/fs_test_dir/test_moved.txt", "./test_output/fs_test_dir/test_renamed.txt")
    print("   Renamed to: ./test_output/fs_test_dir/test_renamed.txt")
    
    // Test 7: fs.delete - delete files
    print("7. Testing fs.delete...")
    fs.delete("./test_output/fs_test_dir/test_renamed.txt")
    fs.delete("./test_output/fs_test_dir/test.txt")
    print("   Deleted files")
    
    // Test 8: fs.chmod - change permissions (Unix only)
    print("8. Testing fs.chmod...")
    fs.touch("./test_output/fs_test_dir/chmod_test.txt")
    fs.chmod("./test_output/fs_test_dir/chmod_test.txt", "644")
    print("   Changed permissions to 644")
    
    // Cleanup
    print("9. Cleaning up...")
    fs.delete("./test_output/fs_test_dir")  // fs.delete handles directories recursively
    print("   Cleaned up test directory")
    
    // Verify cleanup
    if fs.exists("./test_output/fs_test_dir") {
        print("   WARNING: Directory still exists!")
    } else {
        print("   Directory removed successfully")
    }
    
    print("All fs module tests completed!")
}
