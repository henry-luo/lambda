# Test 1: Basic struct creation and field access
Point = Struct.new(:x, :y)
p1 = Point.new(10, 20)
puts p1.x
puts p1.y

# Test 2: Struct with string fields
Person = Struct.new(:name, :age)
p2 = Person.new("Alice", 30)
puts p2.name
puts p2.age

# Test 3: Struct with no args (defaults to nil)
Empty = Struct.new(:a, :b)
e = Empty.new
puts e.a.nil?
