# Test: Ruby string methods (Phase 3)

# upcase / downcase
puts "hello".upcase
puts "HELLO".downcase

# capitalize
puts "hello world".capitalize

# strip / lstrip / rstrip
puts "  hello  ".strip
puts "  hello  ".lstrip.length
puts "  hello  ".rstrip.length

# reverse
puts "abcdef".reverse

# include?
puts "hello world".include?("world")
puts "hello world".include?("xyz")

# start_with? / end_with?
puts "hello".start_with?("hel")
puts "hello".end_with?("llo")

# split
parts = "a,b,c".split(",")
puts parts.length
puts parts[0]
puts parts[2]

# split with no args (whitespace)
words = "hello  world  foo".split
puts words.length
puts words[1]

# gsub
puts "hello world".gsub("o", "0")

# sub (first occurrence only)
puts "hello hello".sub("hello", "hi")

# count
puts "abracadabra".count("a")

# index
puts "hello".index("ll")

# chars
chars = "abc".chars
puts chars.length
puts chars[1]

# empty?
puts "".empty?
puts "hi".empty?

# chomp
puts "hello\n".chomp

# center / ljust / rjust
puts "hi".center(6)
puts "hi".ljust(6, "-")
puts "hi".rjust(6, ".")

# swapcase
puts "Hello World".swapcase

# tr
puts "hello".tr("el", "ip")

# to_i / to_f
puts "42".to_i
puts "3.14".to_f

# concat
puts "hello".concat(" world")

# squeeze
puts "aaabbbccc".squeeze

# delete
puts "hello world".delete("lo")

# slice
puts "hello".slice(1)
puts "hello".slice(1, 3)
