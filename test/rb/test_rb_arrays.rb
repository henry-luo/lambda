# Test: Ruby array methods (Phase 3)

# push / pop
arr = [1, 2, 3]
arr.push(4)
puts arr.length

# pop
val = arr.pop
puts val

# first / last
puts [10, 20, 30].first
puts [10, 20, 30].last

# reverse
puts [1, 2, 3].reverse.join(",")

# min / max / sum
puts [3, 1, 2].min
puts [3, 1, 2].max
puts [1, 2, 3, 4].sum

# include?
puts [1, 2, 3].include?(2)
puts [1, 2, 3].include?(5)

# index
puts [10, 20, 30].index(20)

# join
puts [1, 2, 3].join("-")

# flatten
puts [1, [2, 3], [4]].flatten.length

# compact
puts [1, nil, 2, nil, 3].compact.length

# uniq
puts [1, 2, 2, 3, 3, 3].uniq.length

# sort
puts [3, 1, 2].sort.join(",")

# take / drop
puts [1, 2, 3, 4, 5].take(3).join(",")
puts [1, 2, 3, 4, 5].drop(2).join(",")

# rotate
puts [1, 2, 3, 4].rotate(1).join(",")

# empty?
puts [].empty?
puts [1].empty?

# shift
arr2 = [10, 20, 30]
puts arr2.shift
puts arr2.length

# unshift
arr3 = [2, 3]
arr3.unshift(1)
puts arr3.join(",")

# count with value
puts [1, 2, 2, 3, 2].count(2)

# zip
a = [1, 2, 3]
b = [4, 5, 6]
z = a.zip(b)
puts z.length
puts z[0].join(",")
