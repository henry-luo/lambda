// Port of upstream src/lib/cellMaterials.ts applyAssetVertexColors (MIT).
import color: lambda.slide.color

fn linear(x) => if (x <= 0.04045) x/12.92 else ((x+0.055)/1.055)**2.4
fn srgb(x) => if (x <= 0.0031308) 12.92*x else 1.055*x**(1/2.4)-0.055
fn rgb(hex) array^ => [for (x in slice(color.parse(hex,"cell palette")^,0,3)) linear(x)]
fn mix(a,b,t) => a+(b-a)*t

pub fn bounds(geometries) {
    let per_mesh = [for (geometry in geometries)
        [for (axis in 0 to 2,let values = [for (i in 0 to (len(geometry.positions) div 3-1)) geometry.positions[i*3+axis]])
            {low:min(values),high:max(values)}]]
    let low = [for (axis in 0 to 2) min([for (mesh in per_mesh) mesh[axis].low])]
    let high = [for (axis in 0 to 2) max([for (mesh in per_mesh) mesh[axis].high])];
    {low:low,size:[for (axis in 0 to 2) max(high[axis]-low[axis],0.001)]}
}

pub fn vertices(geometry,cell,extent=null) array^ {
    let positions = geometry.positions
    let box = if (extent == null) bounds([geometry]) else extent
    let low = box.low, size = box.size
    let base = rgb(cell.color)^
    let palette = [base,rgb(cell.accent)^,*[for (part in cell.organelles) rgb(part.color)^]]
    let highlight = rgb("#fff4d8")^, shadow = rgb("#3d4a72")^;
    [for (i in 0 to (len(positions) div 3-1),
        let x = (positions[i*3]-low[0])/size[0],
        let y = (positions[i*3+1]-low[1])/size[1],
        let z = (positions[i*3+2]-low[2])/size[2],
        let flow = sin(x*11.6+y*4.8)+cos(y*9.4+z*7.2),
        let index = int(abs(floor((flow+x*3.2+y*2.6)*len(palette))))%len(palette),
        let tinted = mix(base,palette[index],0.48),
        let lit = mix(tinted,highlight,max(0.0,z-0.24)*0.22),
        let shaded = mix(lit,shadow,max(0.0,0.32-z)*0.12))
        // Three.js mixes in linear space; Scene3D color attributes accept display RGB.
        *[for (channel in shaded) srgb(channel)]]
}
