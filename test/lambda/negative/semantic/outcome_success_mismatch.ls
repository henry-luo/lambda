// S11.4.2v3: disclosing errors does not admit an incompatible normal result.
pn wrong(flag: bool) int^ {
    if (flag) { return null }
    error("disclosed")
}
pn main() { wrong(true) }
