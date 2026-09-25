// Native Makefile lane: --prefix js_ --violations.
import .js_exception_catalog_render

pn main() {
    let failed = print_catalog("js_", true, false)^
    if (failed) { raise error("D8.4.3 exception catalog contract violation") }
}
