// Native Lambda counterpart of utils/generate_well_known_names.py --check.
import .generate_well_known_names_core

pn main() {
    var stale = 0
    for (entry in generated_outputs()) {
        if (not exists(entry.path)) {
            print("stale generated file: " ++ entry.path ++ "\n")
            stale = stale + 1
        } else {
            let actual = input(entry.path, "text")^
            if (actual != entry.content) {
                print("stale generated file: " ++ entry.path ++ "\n")
                stale = stale + 1
            }
        }
    }
    if (stale > 0) { raise error("generated name catalog has " ++ string(stale) ++ " stale file(s)") }
    return
}
