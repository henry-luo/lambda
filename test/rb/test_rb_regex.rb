# Test: Regex support via RE2

# Test 1: =~ operator
puts "hello world" =~ /world/
puts "hello world" =~ /xyz/

# Test 2: !~ operator
puts "hello world" !~ /xyz/
puts "hello world" !~ /world/

# Test 3: .match method
m = "hello world".match(/world/)
puts m

# Test 4: .scan method
result = "abc123def456".scan(/[0-9]+/)
puts result.length
puts result[0]
puts result[1]

# Test 5: .gsub with regex
puts "hello world".gsub(/o/, "0")
puts "abc123def456".gsub(/[0-9]+/, "NUM")

# Test 6: .sub with regex (first match only)
puts "hello world world".sub(/world/, "ruby")
