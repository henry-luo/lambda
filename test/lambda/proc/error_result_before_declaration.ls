// S12.1.2v2: no-result declarations do not overwrite a received error result.
pn retained() error {
    error("retained")
    let no_result = 1
}
pn main() {
    let result = retained();
    result.message
}
