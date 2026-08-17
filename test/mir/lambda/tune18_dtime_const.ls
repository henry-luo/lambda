// Tune18 E2.a: packable datetime literals stay in the MIR scalar lane.

pn main() {
    print(t'2025-04-26' == t'2025-04-26')
}
