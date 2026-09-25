// Native --json report for utils/check_static_module_architecture.py.
import .check_static_module_architecture_core

pn main() {
    print(format(static_module_inventory()^, {type: "json", indent: 2}))
}
