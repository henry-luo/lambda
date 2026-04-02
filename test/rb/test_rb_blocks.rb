# Test: Ruby blocks, iterators, each/map/select/reduce

# each
[1, 2, 3].each { |x| puts x }

# map
result = [1, 2, 3].map { |x| x * 2 }
puts result.length

# select
evens = [1, 2, 3, 4, 5, 6].select { |x| x % 2 == 0 }
puts evens.length

# reduce
sum = [1, 2, 3, 4, 5].reduce(0) { |acc, x| acc + x }
puts sum

# times
3.times { |i| puts i }

# each_with_index
["a", "b", "c"].each_with_index { |x, i| puts "#{i}: #{x}" }
