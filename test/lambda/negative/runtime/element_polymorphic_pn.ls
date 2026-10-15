// S12.1.1v3: a function parameter's colour cannot bypass pure construction.
pn load_parts() { print("bound: procedure ran"); return "prepared" }
function build(f: function) => <doc f()^>
pn main() { return build(load_parts) }
