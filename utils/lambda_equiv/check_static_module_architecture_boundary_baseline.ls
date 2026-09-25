// Native --boundary-baseline report for utils/check_static_module_architecture.py.
import .check_static_module_architecture_core

pn main() {
    let report = static_module_inventory()^
    print(format(report.boundary_failure_baseline, {type: "json", indent: 2}))
}
