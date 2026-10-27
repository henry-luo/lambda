// faceted geology uses native scene buffers and seeded geometry (S12.1.1v3 / D7.5.3v2).
import rng: lambda.procgen.rng
import s: lambda.scene3d
import geometry: ~~.scene3d._geometry
import camera: lambda.scene3d.camera
import solid: .mod_plant_solids
import viewer: .mod_scene_view

pub let CATALOG = [
    {id:"granite",title:"Granite boulders",tag:"ROCK / WEATHERED",note:"Irregular, speckled boulders collect into a small outcrop."},
    {id:"basalt",title:"Basalt columns",tag:"ROCK / COLUMNAR",note:"Close-packed hexagonal columns rise to different cooling heights."},
    {id:"quartz",title:"Quartz cluster",tag:"CRYSTAL / PRISMATIC",note:"Hexagonal prisms terminate in pointed, light-catching facets."},
    {id:"geode",title:"Amethyst geode",tag:"CRYSTAL / CAVITY",note:"A hollow stone shell holds an inward garden of purple crystal points."}
]
pub let COLORWAYS = ["Natural","Twilight","Warm earth"]

fn faceted(id,points,indices,k) map {
    let faces = [for (i in 0 to (len(indices) div 3-1),
        let ids = slice(indices,i*3,i*3+3),let a = points[ids[0]],let b = points[ids[1]],let c = points[ids[2]],
        let n = camera.cross(camera.subtract(b,a),camera.subtract(c,a)) where camera.dot(n,n) > 1e-12)
        {ids:ids,points:[a,b,c],normal:camera.unit(n),shade:rng.between(rng.at(k,i),0.72,1.0)}]
    let edges = unique([for (f in faces) for (j in 0 to 2) sort([f.ids[j],f.ids[(j+1)%3]])]);
    {tree:s.geometry([for (f in faces) for (p in f.points) *p],
        {id:id,normals:[for (f in faces) for (j in 0 to 2) *f.normal],
         colors:[for (f in faces) for (j in 0 to 2) *[f.shade,f.shade,f.shade]]}),
     points:points,edges:edges,faces:len(faces)}
}

fn prism(flat,k) map {
    let points = [*[for (y in [-1.0,(if (flat) 1.0 else 0.6)]) for (i in 0 to 5,let a = float(i)*math.pi/3.0)
        [0.4*cos(a),y,0.4*sin(a)]],[0.0,1.0,0.0],[0.0,-1.0,0.0]]
    let indices = [for (i in 0 to 5,let j = (i+1)%6) *[i,j,i+6,j,j+6,i+6,i+6,j+6,12,j,i,13]];
    faceted(if (flat) "column" else "crystal",points,indices,k)
}

fn rock(k,detail,id) map {
    let rows = 3+detail
    let columns = 6+detail
    let globe = geometry.sphere("source",rows,columns)
    let points = [for (j in 0 to rows,i in 0 to columns,let index = (j*(columns+1)+i)*3,
        let key = rng.at(k,if (j == 0 or j == rows) j*columns else j*columns+i%columns),
        let radius = rng.between(key,0.78,1.1))
        [globe.positions[index]*radius,globe.positions[index+1]*radius,globe.positions[index+2]*radius]];
    faceted(id,points,globe.indices,k)
}

fn bowl(k) map {
    let globe = geometry.sphere("source",8,16)
    let points = [for (j in 0 to 4,i in 0 to 16,let index = ((j+4)*17+i)*3) slice(globe.positions,index,index+3)];
    faceted("bowl",points,geometry.grid_indices(4,16),k)
}

pub fn generate(seed,id,detail,palette) map^ {
    let recipe = [for (r in CATALOG where r.id == id) r][0]
    let k = rng.derive(rng.key(seed),id)
    let count = if (id == "granite") 2+detail else 6+detail*3
    let definitions = if (id == "granite") [for (i in 0 to (count-1)) rock(rng.at(k,i),detail,"stone-"++string(i))]
        else [prism(id == "basalt",k),*(if (id == "geode") [bowl(k)] else [])]
    let colors = if (id == "quartz") ["#c8d9d9","#97bfc8","#d8c4ad"]
        else if (id == "geode") ["#9872b4","#7975b6","#bd8c9d"] else ["#8d958d","#687e86","#a08d77"]
    let pieces = [for (i in 0 to (count-1),let key = rng.at(k,i+100),let a = float(i)*2.4,
        let spread = if (i == 0) 0.0 else sqrt(float(i)/float(count))*(if (id == "geode") 1.3 else 1.6),
        let height = if (id == "granite") rng.between(key,0.5,1.0) else if (id == "geode") rng.between(key,0.35,0.95) else rng.between(key,0.65,1.8),
        let width = if (id == "granite") height*1.25 else rng.between(rng.at(key,1),0.65,1.15))
        solid.oval([spread*cos(a),height+(if (id == "geode") 0.55 else 0.0),spread*sin(a)],[width,height,width],colors[palette],"body",
            [rng.between(rng.at(key,2),-0.1,0.1),a,rng.between(rng.at(key,3),-0.1,0.1)],
            if (id == "granite") "stone-"++string(i) else if (id == "basalt") "column" else "crystal")]
    let shell = if (id == "geode") [solid.oval([0.0,0.9,0.0],[2.1,0.9,2.1],"#8c8178","body",[0.0,0.0,0.0],"bowl"),
        solid.oval([0.0,0.9,0.0],[1.95,0.72,1.95],"#c3a0ce","body",[0.0,0.0,0.0],"bowl")] else []
    let all_parts = [*shell,*pieces]
    let edge_parts = [for (part in all_parts,let definition = [for (d in definitions where d.tree.id == part.shape) d][0])
        for (edge in definition.edges) solid.placed(solid.stem(definition.points[edge[0]],definition.points[edge[1]],0.006,"#4b5b59","edge"),solid.matrix(part))];
    {recipe:recipe,parts:all_parts,edges:edge_parts,resources:definitions |> ~.tree,
     metrics:[{label:"Solid pieces",value:len(all_parts)},{label:"Facet edges",value:len(edge_parts)},{label:"Colorway",value:COLORWAYS[palette]}]}
}

pub fn viewport(model,edges=false) element^ => viewer.viewport([*model.parts,*(if (edges) model.edges else [])],model.resources,
    {id:"mineral-scene",camera_id:"mineral-camera",model_id:"mineral-model",camera:viewer.pose(),label:model.recipe.title,fit:model.parts})^
pub fn draw(model,edges) element^ => viewer.interactive(viewport(model,edges)^,viewer.pose())
