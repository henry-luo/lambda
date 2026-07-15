// Byte-exact regression for binary output, including bytes that are not text.
pn main() {
    let written = output(b'\xDEADBEEF', "./temp/binary_output.bin")^;
    print(written);
}
