// Native Lambda --report output for utils/check_node_module_architecture.py.
import .check_node_module_architecture_core

pn main() {
    print(format(scan_tree(), {type: "json", indent: 2}) ++ "\n")
}
