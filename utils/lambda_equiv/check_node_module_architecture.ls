// Native source/config gate; binary modes need an object-file inspection API.
import .check_node_module_architecture_core

pn main() {
    let report = scan_tree()
    var failed = false
    for (finding in report.violations) {
        print("NODE_MODULE_ARCH: " ++ finding.path ++ ":" ++ string(finding.line) ++
              ": forbidden " ++ finding.token ++ "\n")
        failed = true
    }
    for (finding in check_node_module_link_inputs()) {
        print("NODE_MODULE_ARCH: " ++ finding ++ "\n")
        failed = true
    }
    if (failed) { raise error("NODE_MODULE_ARCH: boundary check failed") }
    print("NODE_MODULE_ARCH: boundary check passed\n")
}
