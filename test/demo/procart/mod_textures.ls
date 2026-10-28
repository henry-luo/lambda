// periodic fields and cell recipes produce real PNG swatches without file IO (S12.1.1v3).
import rng: lambda.procart.rng
import noise: lambda.procart.noise
import img: lambda.image
import media: lambda.doc.media

pub let SIZE = 128
pub let COLORWAYS = ["Natural","Cool mineral","Warm studio"]
pub let CATALOG = [
    {id:"wood",title:"Wood grain",tag:"ORGANIC / FIBERS",note:"Wavy growth fibers flow through a warm, fine-grained surface."},
    {id:"marble",title:"Veined marble",tag:"STONE / VEINS",note:"Narrow, branching-looking veins drift through pale mineral bands."},
    {id:"granite",title:"Granite surface",tag:"STONE / GRAINS",note:"Contrasting mineral grains interrupt a softly mottled stone field."},
    {id:"brick",title:"Brickwork",tag:"MASONRY / COURSES",note:"Staggered brick courses meet at pale mortar joints."},
    {id:"linen",title:"Woven linen",tag:"FABRIC / THREADS",note:"Warp and weft threads alternate over and under one another."},
    {id:"ceramic",title:"Glazed tiles",tag:"CERAMIC / GLAZE",note:"Small glazed tiles catch a soft highlight inside narrow grout lines."}
]

fn mix(a,b,t) array => [for (i in 0 to 2) int(round(float(a[i])+(float(b[i])-float(a[i]))*max(0.0,min(1.0,t))))]
fn fraction(x) float => x-floor(x)

fn colors(id,palette) array {
    if (palette == 1) [[35,68,82],[179,203,203],[231,237,224]]
    else if (palette == 2) [[91,44,48],[202,133,112],[244,222,186]]
    else if (id == "wood") [[73,43,26],[175,121,66],[222,185,127]]
    else if (id == "marble") [[66,80,79],[194,203,194],[242,239,224]]
    else if (id == "brick") [[113,51,40],[190,107,73],[222,205,172]]
    else if (id == "linen") [[114,102,78],[191,178,145],[233,225,202]]
    else if (id == "ceramic") [[36,89,89],[107,163,153],[232,222,191]]
    else [[68,74,73],[150,156,147],[227,224,206]]
}

fn pixel(id,x,y,n,key,detail,palette) array {
    let u = float(x)/float(SIZE)
    let v = float(y)/float(SIZE)
    let c = colors(id,palette)
    let grain = rng.unit(rng.at(key,y*SIZE+x))
    let repeats = float(2+detail);
    if (id == "wood") (let wave = sin(2.0*math.pi*(repeats*v+0.3*sin(2.0*math.pi*u)+n*0.3)),
        mix(c[0],c[2],0.45+0.33*wave+0.045*grain))
    else if (id == "marble") (let vein = abs(sin(2.0*math.pi*(repeats*u+n*1.7))),
        mix(c[0],c[2],min(1.0,vein*5.0)*0.85+0.1*grain))
    else if (id == "granite") mix(if (grain < 0.17) c[0] else c[1],c[2],if (grain > 0.81) 0.95 else 0.12+0.3*(n+0.5))
    else if (id == "brick" or id == "ceramic") {
        let rows = float(2*detail+4)
        let row = int(floor(v*rows))
        let fx = fraction(u*repeats+(if (id == "brick" and row%2 == 1) 0.5 else 0.0))
        let fy = fraction(v*rows)
        let edge = min(min(fx,1.0-fx),min(fy,1.0-fy));
        if (edge < 0.045) c[2]
        else if (id == "brick") mix(c[0],c[1],0.45+0.25*n+0.2*grain)
        else mix(c[0],c[1],0.5+0.35*sin(math.pi*fx)*sin(math.pi*fy)+0.1*n)
    }
    else {
        let weave = float(2*detail+6)
        let a = int(floor(u*weave)),b = int(floor(v*weave))
        let across = if ((a+b)%2 == 0) fraction(u*weave) else fraction(v*weave);
        mix(c[0],c[2],0.3+0.42*sin(math.pi*across)+0.1*grain)
    }
}

pub fn generate(seed,id,detail,palette) map^ {
    let recipe = [for (r in CATALOG where r.id == id) r][0]
    let key = rng.derive(rng.key(seed),id)
    let gen = noise.make(key,{kind:'simplex',fractal:'fbm',octaves:3,frequency:0.012+0.003*float(detail),period:[float(SIZE),float(SIZE)]})^
    let field = noise.field(gen,SIZE,SIZE)
    let samples = reshape([for (y in 0 to (SIZE-1),x in 0 to (SIZE-1))
        for (c in [*pixel(id,x,y,field[y][x],key,detail,palette),255]) u8(c)],[SIZE,SIZE,4])
    let raster = img.from_array(samples)^
    let encoded = img.encode(raster,{format:'png'})^;
    {recipe:recipe,uri:media.data_uri(encoded,"image/png")^,bytes:encoded,field:field,generator:gen,
     metrics:[{label:"Resolution",value:string(SIZE)++" × "++string(SIZE)},
        {label:"Noise octaves",value:3},{label:"Colorway",value:COLORWAYS[palette]}]}
}

pub fn draw(model,tiles) element => <svg class:"specimen material-swatch",viewBox:"0 0 760 540",width:760,height:540,
    xmlns:"http://www.w3.org/2000/svg",role:"img",'aria-label':model.recipe.title,
    for (row in 0 to (if (tiles) 1 else 0),col in 0 to (if (tiles) 1 else 0),let size = if (tiles) 270 else 540)
        <image class:"texture-tile",href:model.uri,x:110+col*size,y:row*size,width:size,height:size>>
