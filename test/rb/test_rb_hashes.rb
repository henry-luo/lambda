# Test: Ruby hash methods (Phase 3)

h = {}
h["name"] = "Alice"
h["age"] = 30

# keys / values
puts h.keys.length
puts h.values.length

# length / size
puts h.length
puts h.size

# has_key? / key?
puts h.has_key?("name")
puts h.has_key?("email")

# fetch
puts h.fetch("name")
puts h.fetch("email", "unknown")

# empty?
empty_h = {}
puts empty_h.empty?
puts h.empty?

# merge
h2 = {"city" => "NYC"}
merged = h.merge(h2)
puts merged.keys.length

# to_a
pairs = h.to_a
puts pairs.length
