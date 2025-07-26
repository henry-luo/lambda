// RST Input Test - Schema Compliance Verification
print("Testing RST Input - Schema Compliance")

let rst = input('./test/input/test.rst', 'rst')

// Display the parsed content to verify schema structure
print("Parsed RST content:")
rst

// Test formatting back to RST
print("\nFormatted back to RST:")
format(rst, 'rst')

"RST schema compliance tests completed!"