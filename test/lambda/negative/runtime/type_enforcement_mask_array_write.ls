fn dynamic(value) => value

pn main() {
    var bytes: u8[] = [1u8, 2u8, 3u8]
    let mask = bytes gt 1
    bytes[mask] = dynamic(300)
}
