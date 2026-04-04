# Test 1: Basic method_missing
class Ghost
  def method_missing(name)
    puts "You called: " + name.to_s
  end
end

g = Ghost.new
g.hello
g.anything

# Test 2: method_missing with arguments
class DynamicGreeter
  def method_missing(name, greeting)
    puts greeting + " from " + name.to_s
  end
end

d = DynamicGreeter.new
d.say_hi("Hi")

# Test 3: method_missing doesn't interfere with existing methods
class Hybrid
  def real_method
    puts "real"
  end

  def method_missing(name)
    puts "missing: " + name.to_s
  end
end

h = Hybrid.new
h.real_method
h.fake_method
