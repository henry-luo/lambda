// S12.1.1v3: dynamic callees cannot cross the pure attribute boundary.
pn load_parts() { print("bound: procedure ran"); return "prepared" }
pn main() {
    let erased: any = load_parts;
    return <doc value: erased()^>
}
