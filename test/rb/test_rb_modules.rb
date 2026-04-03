# Test: Module include

# Test 1: Basic module include
module Greetable
  def greet
    "Hello!"
  end
end

class Person
  include Greetable

  def initialize(n)
    @name = n
  end

  def name
    @name
  end
end

p1 = Person.new("Alice")
puts p1.name
puts p1.greet

# Test 2: Multiple module includes
module Countable
  def count
    42
  end
end

class Robot
  include Greetable
  include Countable

  def initialize(id)
    @id = id
  end

  def robot_id
    @id
  end
end

r = Robot.new("R2D2")
puts r.robot_id
puts r.greet
puts r.count

# Test 3: Module with method using self
module Describable
  def describe
    "I am " + self.label
  end
end

class Item
  include Describable

  def initialize(l)
    @label = l
  end

  def label
    @label
  end
end

item = Item.new("Widget")
puts item.describe
