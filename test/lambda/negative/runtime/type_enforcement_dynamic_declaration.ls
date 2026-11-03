fn dynamic_value() any { 3.5m }

pn main() {
    var value: int = dynamic_value()
    // S7.7.2v2: validation binds its outcome; explicitly deliver it to the CLI.
    value
}
