// Simple Markdown Input Test - Schema Compliance and YAML Frontmatter
// print is a pn (S12.1.1v2): the script runs as a pn main
pn main() {
    print("Testing Markdown Input - Schema Compliance and YAML Support\n")

    // Test 1: Basic markdown without YAML frontmatter
    print("\nTest 1: Basic Markdown\n")
    let md = input('./test/input/test.md', 'markdown')^

    // Verify root structure
    print("Basic document structure:\n")
    print("- Root tag:", md.tag, "\n")
    print("- Has version attribute:", md.attrs["version"] != null, "\n")

    // Display parsed content
    print("\nBasic markdown content:\n")
    print(md)

    // Test 2: Markdown with YAML frontmatter
    print("\n\nTest 2: Markdown with YAML Frontmatter\n")
    let yaml_md = input('./test/input/test_yaml.md', 'markdown')^

    // Verify YAML metadata was parsed
    print("YAML frontmatter document structure:\n")
    print("- Root tag:", yaml_md.tag, "\n")
    print("- Has version attribute:", yaml_md.attrs["version"] != null, "\n")

    // Display YAML document content
    print("\nYAML frontmatter document content:\n")
    print(yaml_md)

    // Test 3: Formatting back to markdown
    print("\n\nTest 3: Format back to Markdown\n")
    print("Basic markdown formatted:\n")
    print(format(md, 'markdown'))

    print("\nYAML frontmatter markdown formatted:\n")
    print(format(yaml_md, 'markdown'))

    print("\nmeta:", yaml_md[0], "\n")
    print("Markdown schema compliance and YAML frontmatter tests completed!\n")
}
