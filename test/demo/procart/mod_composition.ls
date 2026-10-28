// semantic scene descriptions resolve to values before rendering (S12.1.1v3, PG11).
import common: lambda.procart._common
import environment: lambda.procart.environment
import pond: .mod_pond
import rng: lambda.procart.rng
import t: lambda.scene3d.transform

let KINDS = ["flower","tree","stone","grass"]
let GENERATORS = {flower:"plant",tree:"plant",stone:"rock.faceted",grass:"grass.blades"}
let FIELDS = ["id","recipe","seed","key_path","settings","count","spread","position","rotation","scale","matrix"]

pub fn value(v,fallback) => common.value(v,fallback)
fn fail(message) error => error("procart composition: "++message)
pub fn bounded(v,lo,hi,whole=false) bool => common.bounded(v,lo,hi,whole)

pub fn transform(node) array^ {
    if (node.matrix != null) {
        if (not t.affine(node.matrix) or node.position != null or node.rotation != null or node.scale != null)
            raise fail("matrix and position/rotation/scale are exclusive")
        else node.matrix
    }
    else t.matrix(value(node.position,[0.0,0.0,0.0]),value(node.rotation,[0.0,0.0,0.0]),value(node.scale,[1.0,1.0,1.0]))^
}

fn recipe_for(node,recipes) element^ {
    let matches = [for (r in recipes where r.id == node.recipe) r];
    if (len(matches) != 1 or matches[0].kind != string(name(node))) raise fail("missing or incompatible recipe for "++node.id)
    else matches[0]
}

fn inspect(node,recipes,depth=0) map^ {
    let checked_node = if (not (node is element) or depth > 8) raise fail("expected an element within eight group levels")
    let kind = string(name(node))
    let checked_kind = if (kind != "group" and not contains(KINDS,kind)) raise fail("unknown semantic tag "++kind)
    let checked_id = if (not common.identifier(node.id)) raise fail("every object/group needs a nonempty ID of at most 32 letters, digits, - or _")
    let checked_fields = if (any([for (k,v in node where k is string or k is symbol) not contains(FIELDS,string(k))]))
        raise fail("unknown attribute on "++node.id)
    let count = value(node.count,1)
    let checked_count = if (not bounded(count,1,64,true)) raise fail("count must be an integer from 1 to 64")
    let checked_seed = if (node.seed != null and not (node.seed is int or node.seed is string)) raise fail("seed must be an integer or string")
    let checked_path = if (node.key_path != null and (not (node.key_path is array) or len(node.key_path) > 8 or
        not all([for (p in node.key_path) p is int or p is string]))) raise fail("key_path must contain at most eight integer/string parts")
    let checked_spread = if (node.spread != null and (not t.vector(node.spread,2) or min(node.spread) < 0.0 or max(node.spread) > 24.0))
        raise fail("spread must be two finite dimensions from 0 to 24")
    let checked_transform = transform(node)^
    let checked_settings = if (node.settings != null and not (node.settings is map)) raise fail("settings must be a map")
    let checked_leaf = if (kind != "group" and len(content(node)) != 0) raise fail("only groups contain scene objects")
    let checked_group = if (kind == "group" and (node.recipe != null or node.settings != null)) raise fail("groups contain objects; recipes and settings belong to those objects")
    let checked_recipe = if (kind != "group") recipe_for(node,recipes)^
    let children = [for (child in content(node)) inspect(child,recipes,depth+1)^]
    let cost = count*(if (kind == "group") sum(children |> ~.cost) else 1)
    let checked_cost = if (cost > 192) raise fail("composition exceeds 192 expanded objects");
    {ids:[node.id,*[for (child in children) *child.ids]],cost:cost}
}

pub fn validate(doc) bool^ {
    let checked_root = if (not (doc is element) or name(doc) != 'meadow' or doc.version != 1) raise fail("expected meadow version 1")
    let checked_seed = if (not (doc.seed is int or doc.seed is string)) raise fail("meadow needs an integer or string seed")
    let checked_size = if (not bounded(doc.width,4.0,32.0) or not bounded(doc.depth,4.0,32.0) or not bounded(doc.relief,0.0,2.0))
        raise fail("meadow dimensions must be 4..32 and relief 0..2")
    let climate = environment.from_document(doc)^
    let water = pond.from_document(doc)^
    let blocks = [for (c in content(doc) where c is element and name(c) == 'resources') c]
    let checked_resources = if (len(blocks) != 1 or len(content(blocks[0])) < 1 or len(content(blocks[0])) > 32) raise fail("expected one resources block with 1..32 recipes")
    let recipes = content(blocks[0])
    let checked_recipes = [for (r in recipes)
        if (not (r is element) or name(r) != 'recipe' or not common.identifier(r.id) or r.version != 1 or
            not contains(KINDS,r.kind) or not contains([GENERATORS[r.kind],*(if (contains(["flower","grass"],r.kind)) ["plant.herb"] else [])],r.generator) or not (r.parameters is map) or len(content(r)) != 0)
            raise fail("invalid recipe, kind, generator or version") else true]
    let nodes = [for (c in content(doc) where not (c is element and contains(['resources','environment','pond'],name(c)))) c]
    let checked_nodes = if (len(nodes) == 0 or len(nodes) > 64) raise fail("meadow needs 1..64 top-level objects/groups")
    let info = [for (n in nodes) inspect(n,recipes)^]
    let ids = [*[for (r in recipes) r.id],*[for (i in info) *i.ids]]
    let checked_ids = if (len(unique(ids)) != len(ids)) raise fail("duplicate recipe/object ID")
    let checked_budget = if (sum(info |> ~.cost) > 192) raise fail("composition exceeds 192 expanded objects");
    true
}

fn expand(node,recipes,parent_key,parent_matrix,path) array^ {
    // names, rather than sibling positions, own the random streams.
    let base = if (node.seed != null) rng.key(node.seed) else parent_key
    let key = if (node.key_path != null) rng.derive(base,node.key_path)
        else if (node.seed != null) base else rng.derive(base,node.id)
    let own = t.multiply(parent_matrix,transform(node)^)^
    let count = value(node.count,1),spread = value(node.spread,[0.0,0.0]);
    [for (i in 0 to (count-1),let k = rng.at(key,i),let place = rng.derive(k,"placement"),
        let offset = t.matrix([rng.between(place,-spread[0]/2.0,spread[0]/2.0),0.0,
            rng.between(rng.at(place,1),-spread[1]/2.0,spread[1]/2.0)],
            [0.0,if (node.spread != null) rng.between(rng.at(place,2),0.0,2.0*math.pi) else 0.0,0.0],[1.0,1.0,1.0])^,
        let matrix = t.multiply(own,offset)^,let id = path++node.id++"-"++string(i),
        let checked_path = if (len(id) > 96) raise fail("expanded object ID exceeds 96 characters"))
        *(if (name(node) == 'group') [for (child in content(node)) *expand(child,recipes,k,matrix,id++"/")^]
        else (let recipe = recipe_for(node,recipes)^,
            [{id:id,kind:string(name(node)),recipe:recipe.id,key:rng.derive(k,"model"),matrix:matrix,
              generator:recipe.generator,parameters:{*:recipe.parameters,*:value(node.settings,{})}}]))]
}

pub fn resolve(doc) array^ {
    let checked = validate(doc)^
    let recipes = content([for (n in content(doc) where name(n) == 'resources') n][0])
    let identity = t.matrix([0.0,0.0,0.0],[0.0,0.0,0.0],[1.0,1.0,1.0])^;
    [for (node in content(doc) where not contains(['resources','environment','pond'],name(node))) *expand(node,recipes,rng.key(doc.seed),identity,"")^]
}
