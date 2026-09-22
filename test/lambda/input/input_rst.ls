// RST Input Test - Schema Compliance Verification
// print is a pn (S12.1.1v2): the script runs as a pn main
pn main() {
    print("Testing RST Input - Schema Compliance\n")

    let rst = input('./test/input/comprehensive_test.rst', 'rst')^

    // Display the parsed content to verify schema structure
    print("Parsed RST content:\n")
    print(rst)

    print("\nFormat RST:\n")
    print(format(rst, 'json'))

    // Test formatting back to RST
    print("\nFormatted back to RST:\n")
    print(format(rst, 'rst'))

    print("\nRST schema compliance tests completed!\n")
}
