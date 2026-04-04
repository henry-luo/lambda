# Test: File I/O operations

# Test 1: File.write and File.read
File.write("temp/rb_test_io.txt", "hello from ruby")
content = File.read("temp/rb_test_io.txt")
puts content

# Test 2: File.exist?
puts File.exist?("temp/rb_test_io.txt")
puts File.exist?("temp/nonexistent_file_xyz.txt")

# Test 3: File.exists? alias
puts File.exists?("temp/rb_test_io.txt")
