// resource order and traversal order do not own a shape's random stream (PG3, PG12.5–7).
import c: ._common
import rng: .rng
// rule names become invocation tags; recipes and geometry retain reference IDs.
pub fn reference(definition) => if (name(definition) == 'rule') definition.name else definition.id
pub fn valid_name(value:string) bool => c.identifier(value) and contains("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_",slice(value,0,1))
pub fn lookup(definitions,label,kind) element^ {
    let found = [for (d in definitions where string(name(d)) == kind and reference(d) == label) d];
    if (len(found) != 1) raise c.fail("missing " ++ kind ++ " resource " ++ string(label)) else found[0]
}
pub fn key(parent:u64,node,index) u64 {
    let base = if (node.seed != null) rng.key(node.seed) else parent;
    if (node.key_path != null) rng.derive(base,node.key_path)
    else if (node.seed != null) base else rng.derive(base,if (node.id != null) node.id else index)
}
pub fn choose(definition,k:u64) element {
    let variants = sort(content(definition),(n) => n.name);
    rng.weighted(rng.derive(k,"variant"),variants,[for (v in variants) c.value(v.weight,1.0)])
}
