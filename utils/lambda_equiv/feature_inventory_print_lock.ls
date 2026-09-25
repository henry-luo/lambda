// Native Lambda counterpart of feature_inventory.py --print-lock.
import .feature_inventory_core

pn main() {
    print(expected_lock(inventory_rows()^))
    return
}
