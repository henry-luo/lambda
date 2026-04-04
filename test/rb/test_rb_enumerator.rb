# Test 1: flat_map
result = [[1, 2], [3, 4], [5]].flat_map { |a| a }
puts result.length
puts result[0]
puts result[4]

# Test 2: sort_by
words = ["banana", "fig", "apple", "cherry"]
sorted = words.sort_by { |w| w.length }
puts sorted[0]
puts sorted[3]

# Test 3: min_by / max_by
nums = [3, 1, 4, 1, 5, 9]
puts nums.min_by { |n| n }
puts nums.max_by { |n| n }

# Test 4: reduce without initial value
sum = [1, 2, 3, 4, 5].reduce { |acc, n| acc + n }
puts sum

# Test 5: each_with_object
result = [1, 2, 3].each_with_object([]) { |x, arr| arr.push(x * 2) }
puts result.length
puts result[0]
puts result[2]
