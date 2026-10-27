// SVG organs share the turtle's coordinate system, so leaves follow their stems.
import solids: .mod_plant_solids
import transforms: lambda.scene3d.transform
import bloom: .mod_bloom
fn leaf_shape(leaf, color) element => <g class:"leaf",
    transform:"translate("++string(leaf.x)++" "++string(-leaf.y)++") rotate("++string(leaf.angle)++") scale("++string(leaf.size)++")",
    <path d:"M0 0 Q2 -2.8 5.5 0 Q2 2.8 0 0Z",fill:color>
    <path d:"M0 0L4.4 0",fill:"none",stroke:"#e4edcf",'stroke-width':0.18>>

fn blossom(tip, plant) element => <g class:"blossom",
    transform:"translate("++string(tip.x)++" "++string(-tip.y)++") scale("++string(0.34*tip.bloom.size)++")",
    *[for (p in tip.bloom.petals)
        <ellipse cx:0,cy:-2.1*p.length,rx:1.25*p.width,ry:2.8*p.length*cos(p.tilt),fill:plant.petal,stroke:"#f8e5df",'stroke-width':0.15,
            transform:"rotate("++string((tip.bloom.phase+p.angle)*180.0/math.pi)++")">,
      <circle cx:0,cy:0,r:1.25,fill:(if (plant.recipe.center != null) plant.recipe.center else "#dfb75d")>,
      <circle cx:-0.25,cy:-0.3,r:0.5,fill:"#f8df95">]>

fn branch_specimen(plant, show_leaves, show_blooms) element {
    let xs = [for (s in plant.segments) for (x in [s[0],s[2]]) x]
    let ys = [for (s in plant.segments) for (y in [s[1],s[3]]) y]
    // reserve organ-sized margins even when decorations are hidden, keeping the skeleton fixed.
    let x0 = min(xs)-4.0
    let x1 = max(xs)+4.0
    let y0 = min(ys)-4.0
    let y1 = max(ys)+4.0
    let scale = min(640.0/(x1-x0),440.0/(y1-y0))
    let tx = 380.0-(x0+x1)*scale/2.0
    let ty = 490.0+y0*scale;
    <svg class:"specimen",xmlns:"http://www.w3.org/2000/svg",viewBox:"0 0 760 540",width:760,height:540,
        role:"img",'aria-label':plant.recipe.title++" generated from an L-system",
        <ellipse cx:tx,cy:ty+9.0,rx:55,ry:5,fill:"#e0e2cc",opacity:0.65>
        <g transform:"translate("++string(tx)++" "++string(ty)++") scale("++string(scale)++")",
            *[*[for (s in plant.segments) <line class:"stem",x1:s[0],y1:-s[1],x2:s[2],y2:-s[3],
                stroke:plant.recipe.stem,'stroke-width':max(0.18,s[5]*0.16),'stroke-linecap':"round">],
              *(if (show_leaves) [for (l in plant.leaves) leaf_shape(l,plant.recipe.leaf)] else []),
              *(if (show_blooms and plant.recipe.petals > 0) [for (tip in plant.tips) blossom(tip,plant)]
                else [for (tip in plant.tips) <circle class:"bud",cx:tip.x,cy:-tip.y,r:0.18,fill:plant.recipe.stem>])]> >
}

fn solid_shape(p) element {
    let cx = p.position[0]
    let cy = -p.position[1]
    let attributes = {class:p.role,fill:p.color,transform:"translate("++string(cx)++" "++string(cy)++") rotate("++string(-p.rotation[2]*180.0/math.pi)++")"};
    if (contains(["petal","round-petal","oak-leaf","fan-leaf"],p.shape)) {
        let m = solids.matrix(p)
        let steps = if (p.shape == "oak-leaf") 48 else if (p.shape == "petal") 8 else 24
        // fan leaves have a curved distal rim, unlike petals whose two sides meet at the tip.
        let edge = if (p.shape == "fan-leaf") [transforms.point(m,bloom.point(0.0,0.0,p.shape))^,
            *[for (i in 0 to steps) transforms.point(m,bloom.point(1.0,2.0*float(i)/float(steps)-1.0,p.shape))^]]
            else [for (side in [-1.0,1.0]) for (i in 0 to steps)
                transforms.point(m,bloom.point((if (side < 0) float(i) else float(steps-i))/float(steps),side,p.shape))^];
        <polygon class:p.role,fill:p.color,points:join([for (q in edge) string(q[0])++","++string(-q[1])]," ")>
    }
    else if (p.shape == "pad") {
        let m = solids.matrix(p)
        let edge = [transforms.point(m,[0.0,0.0,0.0])^,
            *[for (i in 0 to 48) transforms.point(m,bloom.point(1.0,float(i)/48.0,"pad"))^]];
        <polygon class:p.role,fill:p.color,points:join([for (q in edge) string(q[0])++","++string(-q[1])]," ")>
    }
    else if (p.shape == "dome") <path *:attributes,d:"M-1 0 A1 1 0 0 1 1 0Z",
        transform:attributes.transform++" scale("++string(p.scale[0])++" "++string(p.scale[1])++")">
    else {
        // project the rotated ellipsoid, including foreshortening of radial gills and petals.
        let m = solids.matrix(p)
        let xx = sum([for (i in 0 to 2) m[i*4]**2])
        let yy = sum([for (i in 0 to 2) m[i*4+1]**2])
        let xy = -sum([for (i in 0 to 2) m[i*4]*m[i*4+1]])
        let delta = sqrt((xx-yy)**2+4.0*xy**2)
        let rotation = math.atan2(2.0*xy,xx-yy)*90.0/math.pi;
        <ellipse class:p.role,fill:p.color,cx:cx,cy:cy,rx:sqrt((xx+yy+delta)/2.0),ry:sqrt(max(0.0,(xx+yy-delta)/2.0)),
            transform:"rotate("++string(rotation)++" "++string(cx)++" "++string(cy)++")">
    }
}

// new solid recipes use the same organ records as their native 3D counterparts.
fn illustrated_parts(plant,leaves=true,flowers=true) array {
    let parts = solids.pieces(plant,leaves,flowers);
    if (plant.recipe.illustration_pitch == null) parts
    else (let m = transforms.matrix([0.0,0.0,0.0],[plant.recipe.illustration_pitch,0.0,0.0],[1.0,1.0,1.0])^,
        [for (part in parts) solids.placed(part,m)])
}

fn solid_specimen(plant,show_leaves,show_blooms) element {
    let box = solids.bounds(illustrated_parts(plant))
    let scale = min(640.0/(box.hi[0]-box.lo[0]),440.0/(box.hi[1]-box.lo[1]))
    let tx = 380.0-(box.hi[0]+box.lo[0])*scale/2.0
    let ty = 490.0+box.lo[1]*scale;
    <svg class:"specimen",xmlns:"http://www.w3.org/2000/svg",viewBox:"0 0 760 540",width:760,height:540,
        role:"img",'aria-label':plant.recipe.title,
        <ellipse cx:380,cy:490,rx:115,ry:7,fill:"#e0e2cc">
        <g transform:"translate("++string(tx)++" "++string(ty)++") scale("++string(scale)++")",
            *[for (p in illustrated_parts(plant,show_leaves,show_blooms) order by p.position[2]) solid_shape(p)]>>
}

pub fn specimen(plant,show_leaves,show_blooms) element =>
    if (plant.solids == null) branch_specimen(plant,show_leaves,show_blooms) else solid_specimen(plant,show_leaves,show_blooms)
