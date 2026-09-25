// Native Lambda counterpart of feature_inventory.py --list.
import .feature_inventory_core

pn main() {
    for (row in inventory_rows()^) { print(row ++ "\n") }
    return
}
