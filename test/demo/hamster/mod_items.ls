// furnishings are room-owned values, so moving, deleting and undoing a room carries its contents.
pub let catalog = [
    {kind:"bowl",title:"Seed bowl",tag:"FOOD",slot:"bowl",color:"#a9bea4",note:"A place to eat refilled seeds"},
    {kind:"bottle",title:"Water bottle",tag:"WATER",slot:"bottle",color:"#a9c9cb",note:"A fresh drink in any room"},
    {kind:"hideout",title:"Cozy hideout",tag:"REST",slot:"center",color:"#cba77e",note:"A sheltered place to sleep"},
    {kind:"wheel",title:"Running wheel",tag:"TOY",slot:"center",color:"#9fbcb6",note:"Exercise and contentment"},
    {kind:"sand",title:"Sand bath",tag:"CARE",slot:"center",color:"#d6c599",note:"A sandy grooming spot"},
    {kind:"bridge",title:"Climbing bridge",tag:"TOY",slot:"center",color:"#bf9f79",note:"A little climbing adventure"},
    {kind:"chew",title:"Chew sticks",tag:"TOY",slot:"chew",color:"#b89972",note:"Something to nibble and investigate"}
]
pub fn entry(kind) => [for (e in catalog where e.kind == kind) e][0]
pub fn defaults(kind) => if (kind == "kitchen") ["bowl","bottle"] else if (kind == "nest") ["hideout"]
    else if (kind == "wheel" or kind == "sand") [kind] else []
pub fn contents(m) => if (m.items == null) defaults(m.kind) else m.items
pub fn has(m,kind) => m != null and contains(contents(m),kind)
pub fn accepts(m) => m != null and not contains(["ramp","tunnel","bend"],m.kind)
pub fn valid(m) => m.items == null or (m.items is array and len(m.items)<=4 and
    (accepts(m) or len(m.items) == 0) and all([for (kind in m.items,let item = entry(kind))
        item != null and len([for (other in m.items where entry(other).slot == item.slot) other]) == 1]))
pub fn count(modules) => sum([for (m in modules) len(contents(m))])
pub fn serves(m,intent) => if (intent == "eat") has(m,"bowl") else if (intent == "drink") has(m,"bottle")
    else if (intent == "sleep") has(m,"hideout") else if (intent == "exercise") has(m,"wheel")
    else if (intent == "bathe") has(m,"sand") else if (intent == "play") has(m,"bridge") or has(m,"chew") else intent == "explore"
