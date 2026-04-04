# Test: Comparable via <=> on custom classes

# Test 1: class with <=> for comparison
class Temperature
  def initialize(degrees)
    @degrees = degrees
  end

  def <=>(other)
    @degrees <=> other.degrees
  end

  def degrees
    @degrees
  end

  def to_s
    @degrees.to_s
  end
end

a = Temperature.new(50)
b = Temperature.new(70)
c = Temperature.new(50)

# Test 1: spaceship operator
puts a <=> b

# Test 2: less than via <=>
puts a < b

# Test 3: greater than via <=>
puts b > a

# Test 4: less than or equal
puts a <= c

# Test 5: greater than or equal
puts b >= a
