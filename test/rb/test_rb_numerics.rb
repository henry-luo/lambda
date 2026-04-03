# Test: Ruby integer and float methods (Phase 3)

# Integer methods
puts 42.even?
puts 43.even?
puts 43.odd?
puts 0.zero?
puts 5.positive?
puts(-3.negative?)
puts(-5.abs)

# gcd / lcm
puts 12.gcd(8)
puts 4.lcm(6)

# digits
puts 123.digits.join(",")

# Float methods
puts 3.7.round
puts 3.7.floor
puts 3.2.ceil
puts 3.7.truncate

puts 3.14159.round(2)
puts(-2.5.abs)

puts 0.0.zero?

# to_s / to_i conversions
puts 42.to_s
puts 3.14.to_i

# between?
puts 5.between?(1, 10)
puts 15.between?(1, 10)

# clamp
puts 5.clamp(1, 10)
puts 15.clamp(1, 10)
puts(-3.clamp(0, 100))

# chr
puts 65.chr
