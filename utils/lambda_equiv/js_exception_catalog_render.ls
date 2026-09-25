// Print the D8.4.3 census as the Python checker prints each row.
import .js_exception_catalog_core

pn pad(value: string, width: int) {
    var out = value
    while (len(out) < width) { out = out ++ " " }
    return out
}

pub pn print_catalog(prefix, violations_only: bool, show_void: bool) bool^ {
    let report = census(prefix)^
    let failed = len(report.tier_a) + len(report.tier_b) + len(report.tier_c) > 0
    if (not violations_only) {
        let scope = if (prefix == null) "all" else prefix
        print("exception_effect census (" ++ scope ++
              " rows in lambda/runtime/sys_func_registry.c)\n")
        print("  total rows            : " ++ string(len(report.selected)) ++ "\n")
        for (effect in ["MAY_SET(default)", "PRESERVES", "CLEARS", "SETS"]) {
            var count = 0
            for (row in report.selected) {
                if (row.effect == effect) { count = count + 1 }
            }
            print("  " ++ pad(effect, 22) ++ ": " ++ string(count) ++ "\n")
        }
        print("\nrows with an explicit non-default effect:\n")
        for (row in rows_by_name(report.selected)) {
            if (row.effect != "MAY_SET(default)") {
                print("  " ++ pad(row.effect, 10) ++ " " ++ row.name ++ "\n")
            }
        }
    }

    print("\nD8.4.3 tier A -- raw-scalar helper lacking a PRESERVES contract: " ++
          string(len(report.tier_a)) ++ "\n")
    if (len(report.tier_a) > 0) {
        print("  " ++ pad("registry name", 44) ++ pad("target", 40) ++
              pad("C return", 12) ++ "catalog effect\n")
        for (finding in report.tier_a) {
            print("  " ++ pad(finding.name, 44) ++ pad(finding.target, 40) ++
                  pad(finding.ret, 12) ++ finding.value ++ "\n")
        }
        print("\n  Declare NON_GC_SCALAR plus PRESERVES, or return a boxed Item.\n")
    }

    print("\nD8.4.3 tier B -- void helper whose row still claims MAY_SET: " ++
          string(len(report.tier_b)) ++ "\n")
    if (len(report.tier_b) > 0) {
        if (show_void) { for (finding in report.tier_b) {
            print("  " ++ finding.name ++ "\n")
        } }
        if (not show_void) { print("  (use --show-void to list)\n") }
    }
    print("  A void helper cannot deliver a replacement error Item; its row\n")
    print("  must say PRESERVES so the emitter retains the preceding carrier\n")
    print("  instead of clearing it fail-closed.\n")

    let tier_c_header = "\nD8.4.3 tier C -- boxed-Item helper cataloged as a raw scalar: " ++
                        string(len(report.tier_c))
    if (len(report.tier_c) > 0) {
        print(tier_c_header ++ "\n")
        print("  " ++ pad("registry name", 44) ++ pad("target", 40) ++
              pad("C return", 12) ++ "catalog class\n")
        for (finding in report.tier_c) {
            print("  " ++ pad(finding.name, 44) ++ pad(finding.target, 40) ++
                  pad(finding.ret, 12) ++ finding.value ++ "\n")
        }
        print("\n  An Item-returning helper must not be declared NON_GC_SCALAR.\n")
    } else if (failed) {
        print(tier_c_header ++ "\n")
    } else {
        // A successful main adds its own final newline.
        print(tier_c_header)
    }
    return failed
}
