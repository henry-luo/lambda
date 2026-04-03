# Test: Heredoc support

# Test 1: Basic heredoc
text = <<HEREDOC
Hello World
This is a heredoc
HEREDOC
puts text.strip

# Test 2: Squiggly heredoc (strips leading whitespace)
text2 = <<~HEREDOC
  Hello
  World
HEREDOC
puts text2.strip

# Test 3: Heredoc with single quotes (no interpolation)
name = "Ruby"
text3 = <<'END'
Hello #{name}
END
puts text3.strip
