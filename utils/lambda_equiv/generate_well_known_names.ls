// Native Lambda port of utils/generate_well_known_names.py.
import .generate_well_known_names_core

pn main() {
    for (entry in generated_outputs()) {
        let written = output(entry.content, entry.path, "text")^
    }
    return
}
