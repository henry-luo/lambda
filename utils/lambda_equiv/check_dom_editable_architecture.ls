// Native gate for utils/lint/rules/structural/check_dom_editable_architecture.py.
import .check_dom_editable_architecture_core

pn main() {
    let failures = edit_architecture_failures()^
    if (len(failures) > 0) {
        print("dom-editable-architecture: boundary violations:\n")
        for (failure in failures) { print("  " ++ failure ++ "\n") }
        raise error("dom-editable-architecture: boundary violations")
    }
    print("dom-editable-architecture: shared registry has one owner and " ++
          "the ambient selection ABI remains retired\n")
}
