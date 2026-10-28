// seeded SVG ornament shares a finite canvas and palette controls (S12.1.1v3).
import rng: lambda.procart.rng
import sample: lambda.procart.sample
import geom: lambda.procart.geom

pub let COLORWAYS = ["Indigo & gold","Garden earth","Jewel tones"]
let PALETTES = [["#304b63","#d7a54c","#dc836f","#8caeaa"],["#53715b","#a5b38b","#c18a6a","#d8bb88"],
    ["#476e7d","#9c7caa","#ddad6b","#71a99a"]]
pub let CATALOG = [
    {id:"mandala",title:"Radial mandala",tag:"RADIAL / PETALS",note:"Nested petal rings rotate around a shared center."},
    {id:"stars",title:"Geometric stars",tag:"TILES / OCTAGRAMS",note:"Eight-point stars repeat through a grid of contrasting borders."},
    {id:"truchet",title:"Truchet paths",tag:"TILES / ARCS",note:"Seeded quarter-circle tiles join into looping paths."},
    {id:"weave",title:"Ribbon weave",tag:"LATTICE / INTERLACE",note:"Alternating crossings lift colored ribbons over and under each other."},
    {id:"mosaic",title:"Voronoi mosaic",tag:"CELLS / MOSAIC",note:"Jittered sites divide a square into irregular colored cells."},
    {id:"spiral",title:"Golden spiral",tag:"PHYLLOTAXIS / DOTS",note:"A golden-angle spiral distributes hundreds of marks into a circular ornament."}
]

fn points(values) string => join([for (p in values) string(p[0])++","++string(p[1])]," ")
fn mandala(k,d,c) array => [for (ring in 1 to (d+2),let r = float(ring)*210.0/float(d+3),let n = 10+d*2,
    let phase = rng.between(rng.at(k,ring),0.0,18.0))
    *[for (i in 0 to (n-1)) <ellipse cx:380,cy:270.0-r,rx:r*0.16,ry:22.0,
        fill:c[ring%4],stroke:"#faf5e8",'stroke-width':1.1,
        transform:"rotate("++string(float(i)*360.0/float(n)+phase)++" 380 270)">,
      <circle cx:380,cy:270,r:r,fill:"none",stroke:c[(ring+1)%4],'stroke-width':1.1>]]

fn stars(k,d,c) array => [for (y in 0 to (d+2),x in 0 to (d+2),let size = 480.0/float(d+3),
    let cx = 140.0+(float(x)+0.5)*size,let cy = 30.0+(float(y)+0.5)*size,
    let rotation = rng.between(rng.at(k,y*31+x),-0.08,0.08))
    *[<rect x:cx-size/2.0+2.0,y:cy-size/2.0+2.0,width:size-4.0,height:size-4.0,fill:c[(x+y)%4],opacity:0.22>,
      <polygon points:points([for (i in 0 to 15,let a = float(i)*math.pi/8.0+rotation,let r = size*(if (i%2 == 0) 0.43 else 0.22))
          [cx+r*cos(a),cy+r*sin(a)]]),fill:c[(x+2*y)%4],stroke:"#faf5e8",'stroke-width':1.3>]]

fn truchet(k,d,c) array => [for (y in 0 to (d+4),x in 0 to (d+4),let size = 480.0/float(d+5),
    let turn = rng.below(rng.at(k,y*43+x),2))
    <g transform:"translate("++string(140.0+float(x)*size)++" "++string(30.0+float(y)*size)++") rotate("++string(turn*90)++" "++string(size/2.0)++" "++string(size/2.0)++")",
        <rect width:size,height:size,fill:c[0],opacity:0.12>
        <path d:"M0 "++string(size/2.0)++" A"++string(size/2.0)++" "++string(size/2.0)++" 0 0 0 "++string(size/2.0)++" 0 M"++string(size/2.0)++" "++string(size)++" A"++string(size/2.0)++" "++string(size/2.0)++" 0 0 1 "++string(size)++" "++string(size/2.0),
            fill:"none",stroke:c[1+turn],'stroke-width':size*0.12,'stroke-linecap':"round">>]

fn weave(k,d,c) array {
    let n = d+5
    let size = 480.0/float(n)
    let width = size*0.43
    let offset = rng.below(k,4);
    [*[for (i in 0 to (n-1)) <rect x:140.0+(float(i)+0.5)*size-width/2.0,y:30,width:width,height:480,fill:c[(i+offset)%4]>],
     *[for (i in 0 to (n-1)) <rect x:140,y:30.0+(float(i)+0.5)*size-width/2.0,width:480,height:width,fill:c[(i+1+offset)%4]>],
     *[for (y in 0 to (n-1),x in 0 to (n-1) where (x+y)%2 == 0)
        <rect x:140.0+(float(x)+0.5)*size-width/2.0,y:30.0+(float(y)+0.5)*size-width/2.0-2.0,width:width,height:width+4.0,
            fill:c[(x+offset)%4],stroke:"#f5ebd6",'stroke-width':1.0>]]
}

fn mosaic(k,d,c) array^ {
    let sites = sample.jitter(k,{width:480.0,height:480.0,cell:480.0/float(d+4)})^
    let cells = geom.voronoi(geom.delaunay(sites)^,{bounds:[0.0,0.0,480.0,480.0]})^;
    [for (i,cell in cells) <polygon points:points([for (p in cell) [140.0+p[0],30.0+p[1]]]),
        fill:rng.pick(rng.at(k,i),c),stroke:"#faf5e8",'stroke-width':2.2>]
}

fn spiral(k,d,c) array {
    let n = 120+d*60
    let phase = rng.between(k,0.0,2.0*math.pi);
    [for (i in 0 to (n-1),let a = float(i)*2.399963229728653+phase,let r = 222.0*sqrt(float(i)/float(n)))
        <ellipse cx:380.0+r*cos(a),cy:270.0+r*sin(a),rx:3.0+float(d)*0.4,ry:5.5,
            transform:"rotate("++string(a*180.0/math.pi)++" "++string(380.0+r*cos(a))++" "++string(270.0+r*sin(a))++")",fill:c[i%4]>]
}

pub fn generate(seed,id,detail,palette) map^ {
    let recipe = [for (r in CATALOG where r.id == id) r][0]
    let k = rng.derive(rng.key(seed),id)
    let makers = {mandala:mandala,stars:stars,truchet:truchet,weave:weave,mosaic:mosaic,spiral:spiral}
    let nodes = makers[id](k,detail,PALETTES[palette])^;
    {recipe:recipe,nodes:nodes,detail:detail,
     metrics:[{label:"Drawn marks",value:len(nodes)},{label:"Complexity",value:detail},{label:"Colorway",value:COLORWAYS[palette]}]}
}

pub fn draw(model,guides) element => <svg class:"specimen decorative-pattern",viewBox:"0 0 760 540",width:760,height:540,
    xmlns:"http://www.w3.org/2000/svg",role:"img",'aria-label':model.recipe.title,
    <rect width:760,height:540,fill:"#fcf9f0">
    <g class:"pattern-marks",*model.nodes>
    if (guides) <g class:"pattern-guides",fill:"none",stroke:"#796a56",opacity:0.35,'stroke-dasharray':"4 4",
        *[<rect x:140,y:30,width:480,height:480>,<line x1:380,y1:20,x2:380,y2:520>,<line x1:130,y1:270,x2:630,y2:270>,
          *[for (r in [80,160,240]) <circle cx:380,cy:270,r:r>]]>>
