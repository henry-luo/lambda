// Simple Markdown Input Test - Schema Compliance and YAML Frontmatter
print("Testing Markdown Input - Schema Compliance and YAML Support")

// Test 1: Basic markdown without YAML frontmatter
print("\nTest 1: Basic Markdown")
let md = input('./test/input/test.md', 'markdown')

// Verify root structure
print("Basic document structure:")
print("- Root tag:", md.tag)
print("- Has version attribute:", md.attrs["version"] != null)

// Display parsed content
print("\nBasic markdown content:")
md

// Test 2: Markdown with YAML frontmatter  
print("\n\nTest 2: Markdown with YAML Frontmatter")
let yaml_md = input('./test/input/test_yaml.md', 'markdown')

// Verify YAML metadata was parsed
print("YAML frontmatter document structure:")
print("- Root tag:", yaml_md.tag) 
print("- Has version attribute:", yaml_md.attrs["version"] != null)

// Display YAML document content
print("\nYAML frontmatter document content:")
yaml_md

// Test 3: Formatting back to markdown
print("\n\nTest 3: Format back to Markdown")
print("Basic markdown formatted:")
format(md, 'markdown')

print("\nYAML frontmatter markdown formatted:")
format(yaml_md, 'markdown')

"Markdown schema compliance and YAML frontmatter tests completed!"
