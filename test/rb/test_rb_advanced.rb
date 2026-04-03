# Phase 4: Advanced features tests

# Test 1: nil?
puts nil.nil?
puts 42.nil?
puts "hello".nil?

# Test 2: class
puts 42.class
puts "hello".class
puts true.class
puts nil.class
puts [1,2].class

# Test 3: respond_to?
puts "hello".respond_to?("upcase")
puts "hello".respond_to?("nonexistent")
puts 42.respond_to?("even?")

# Test 4: send
puts "hello".send("upcase")
puts "hello world".send("split", " ").length
puts 42.send("even?")

# Test 5: is_a? with class instances
class Animal
  def initialize(name)
    @name = name
  end
end

a = Animal.new("Dog")
puts a.nil?

# Test 6: exception handling with method calls
begin
  result = "HELLO".downcase
  puts result
rescue => e
  puts "error: #{e}"
end

# Test 7: ensure with return value
x = begin
  42
rescue
  0
end
puts x
