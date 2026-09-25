// Native Lambda port of test/fuzzy/lambda/feature_inventory.py.
import .feature_inventory_core

pn main() {
    print(check_inventory()^ ++ "\n")
    return
}
