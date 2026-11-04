// Native default census and D7.4.3 lint.
import .js_exception_catalog_render

pn main() {
    let failed = print_catalog(null, false, false)^
    if (failed) { raise error("D7.4.3 exception catalog contract violation") }
}
