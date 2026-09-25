// Native --violations output across all catalog rows.
import .js_exception_catalog_render

pn main() {
    let failed = print_catalog(null, true, false)^
    if (failed) { raise error("D8.4.3 exception catalog contract violation") }
}
