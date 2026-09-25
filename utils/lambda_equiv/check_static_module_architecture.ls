// Native default report for utils/check_static_module_architecture.py.
import .check_static_module_architecture_core

pn main() {
    let report = static_module_inventory()^
    let headers = report.public_header_providers
    let includes = report.include_edges
    let scaffold = report.validation_dso_scaffold
    let baseline = report.boundary_failure_baseline
    var frozen = 0
    for (item in headers) { if (item.frozen) { frozen = frozen + 1 } }
    print("STATIC_MODULE_ARCH: report-only P0 inventory\n")
    print("STATIC_MODULE_ARCH: headers=" ++ string(len(headers)) ++
          " frozen=" ++ string(frozen) ++ "\n")
    print("STATIC_MODULE_ARCH: lib_to_lambda=" ++ string(len(includes.lib_to_lambda)) ++
          " upper_to_radiant=" ++ string(len(includes.upper_to_radiant)) ++
          " runtime_native_io=" ++ string(len(report.runtime_native_io)) ++ "\n")
    print("STATIC_MODULE_ARCH: predicted_dso_externs=" ++
          string(len(baseline.upward_externs)) ++ " weak_providers=" ++
          string(len(baseline.weak_providers)) ++ "\n")
    print("STATIC_MODULE_ARCH: public_lambda_static_selectors=" ++
          string(len(report.public_header_macro_hygiene)) ++ "\n")
    if (len(scaffold.missing) > 0 or len(scaffold.unexpected) > 0 or
        not contains(["report-only", "enforced-with-class-f-defer"], scaffold.mode)) {
        raise error("STATIC_MODULE_ARCH: validation DSO scaffold needs repair")
    }
    if (scaffold.mode == "enforced-with-class-f-defer") {
        print("STATIC_MODULE_ARCH: five-DSO harness active; Class-F stays ratcheted by module-boundary-link")
    } else {
        print("STATIC_MODULE_ARCH: five-DSO scaffold declared; enforcement deferred to P5")
    }
}
