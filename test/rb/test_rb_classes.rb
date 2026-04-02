# Test: Ruby classes, instance variables, methods, inheritance
# Phase 2 OOP features

# Basic class with initialize and methods
class Dog
  def initialize(name, age)
    @name = name
    @age = age
  end

  def speak
    puts "Woof! I'm #{@name}"
  end

  def info
    puts "#{@name} is #{@age} years old"
  end
end

dog = Dog.new("Rex", 5)
dog.speak
dog.info

# Inheritance
class Puppy < Dog
  def initialize(name)
    @name = name
    @age = 0
  end

  def speak
    puts "Yip! I'm #{@name}"
  end
end

puppy = Puppy.new("Tiny")
puppy.speak
puppy.info
