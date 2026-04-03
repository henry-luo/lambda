# Phase 4: Exception handling tests

# Test 1: basic begin/rescue
begin
  raise "something went wrong"
rescue => e
  puts e
end

# Test 2: raise with no rescue — captured by outer rescue
begin
  begin
    raise "inner error"
  end
rescue => e
  puts "caught: #{e}"
end

# Test 3: rescue specific error type
begin
  raise RuntimeError, "runtime problem"
rescue RuntimeError => e
  puts "RuntimeError: #{e}"
end

# Test 4: ensure block always runs
begin
  puts "try block"
  raise "oops"
rescue => e
  puts "rescued: #{e}"
ensure
  puts "ensure runs"
end

# Test 5: begin/rescue with else (no exception)
begin
  x = 10 + 20
rescue => e
  puts "should not print"
else
  puts "else: #{x}"
ensure
  puts "ensure after else"
end

# Test 6: inline rescue
result = raise("fail") rescue "default"
puts result

# Test 7: nested begin/rescue
begin
  begin
    raise "deep error"
  rescue => e
    puts "inner: #{e}"
    raise "re-raised"
  end
rescue => e
  puts "outer: #{e}"
end

# Test 8: rescue with no variable binding
begin
  raise "no var"
rescue
  puts "rescued without variable"
end

# Test 9: raise with two args (type, message)
begin
  raise TypeError, "wrong type"
rescue TypeError => e
  puts "TypeError: #{e}"
end

# Test 10: retry
attempts = 0
begin
  attempts = attempts + 1
  raise "retry me" if attempts < 3
  puts "succeeded after #{attempts} attempts"
rescue => e
  retry if attempts < 3
  puts "gave up"
end

# Test 11: multiple rescue clauses
begin
  raise ArgumentError, "bad arg"
rescue TypeError => e
  puts "type error"
rescue ArgumentError => e
  puts "ArgumentError: #{e}"
rescue => e
  puts "generic"
end

# Test 12: ensure runs even with successful execution
begin
  puts "success path"
ensure
  puts "ensure after success"
end
