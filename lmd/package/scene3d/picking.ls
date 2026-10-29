// S12.1.1v3 / D7.2.4v4: queries consume pure values and never retain a native viewport.
import c: .camera
import t: .transform

fn value(v,fallback) => if (v==null) fallback else v
fn column_scale(world,axis) => sqrt(sum([for (i in 0 to 2) world[axis*4+i]**2]))
fn triangle_hits(camera,triangles,x,y) => [for (triangle in triangles, let hit=c.triangle_hit(c.ray(camera,x,y),triangle[0],triangle[1],triangle[2]) where hit!=null)
    hit]
fn nearest(hits) => if (len(hits)==0) null else (let distance=min([for (hit in hits) hit.distance]),[for (hit in hits where hit.distance==distance) hit][0])
fn face(camera,point,size,center,world,screen) {
    let p=c.project(camera,point);
    if (p.depth<camera.near or (not camera["infinite-far"] and p.depth>camera.far)) [] else {
        let extent=if (screen) size else [c.pixel_radius(camera,point,[1,0],size[0]*column_scale(world,0)),c.pixel_radius(camera,point,[0,1],size[1]*column_scale(world,1))];
        let corners=[for (uv in [[0,0],[1,0],[1,1],[0,1]]) c.unproject(camera,p.x+(uv[0]-center[0])*extent[0],p.y-(uv[1]-center[1])*extent[1],p.depth)];
        [for (indices in [[0,1,2],[0,2,3]]) [for (i in indices) corners[i]]]
    }
}
fn point_hit(camera,point,size,world,screen,x,y) {
    let p=c.project(camera,point);
    if (p.depth<camera.near or (not camera["infinite-far"] and p.depth>camera.far)) null else {
        let rx=if (screen) size/2 else c.pixel_radius(camera,point,[1,0],size*column_scale(world,0)/2);
        let ry=if (screen) size/2 else c.pixel_radius(camera,point,[0,1],size*column_scale(world,1)/2);
        if (((x-p.x)/rx)**2+((y-p.y)/ry)**2>1) null else {
            let position=c.unproject(camera,x,y,p.depth);
            let ray=c.ray(camera,x,y);
            {position:position,distance:c.dot(c.subtract(position,ray.origin),ray.direction)}
        }
    }
}
fn bevel(camera,before,center,after,width,world_width) {
    let left=c.clip_near(camera,center,before),right=c.clip_near(camera,center,after);
    let p=c.project(camera,center);
    if (p.depth<camera.near or len(left)==0 or len(right)==0) [] else {
        let a=c.project(camera,left[1]),b=c.project(camera,right[1]);
        let lp=math.hypot(p.x-a.x,p.y-a.y),ln=math.hypot(b.x-p.x,b.y-p.y);
        if (lp==0 or ln==0) [] else {
            let prev=[(p.x-a.x)/lp,(p.y-a.y)/lp],next=[(b.x-p.x)/ln,(b.y-p.y)/ln];
            let turn=prev[0]*next[1]-prev[1]*next[0];
            if (turn==0) [] else {
                let sign=if (turn>0) -1 else 1;
                let outer=[for (direction in [prev,next], let axis=[0-direction[1],direction[0]], let radius=if (world_width) c.pixel_radius(camera,center,axis,width/2) else width/2)
                    c.unproject(camera,p.x+sign*axis[0]*radius,p.y+sign*axis[1]*radius,p.depth)];
                [[center,outer[0],outer[1]]]
            }
        }
    }
}

// geometry is resolved source buffer data; world is the final group/object/instance matrix.
// hits follow geometry coverage, without texture-alpha testing, sorting or mesh occlusion.
pub fn hit(camera,node,geometry,x,y,world=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]) {
    let kind=name(node);
    // S7.4.1: an invalid query returns a value error before geometry work.
    if (not contains(['points','sprite','line','line-segments'],kind) or geometry.type!='buffer' or not t.affine(world)) error("scene3d.picking: expected a drawable, source buffer and final affine matrix")
    else {
    let count=len(geometry.positions)/3;
    let indices=if (geometry.indices!=null and len(geometry.indices)>0) geometry.indices else [for (i in 0 to (count-1)) i];
    let positions=[for (i in indices) t.point(world,slice(geometry.positions,i*3,i*3+3))^];
    let screen=value(node["size-mode"],if (kind=='sprite') 'world' else 'screen')=='screen';
    let width=value(node.width,1)*(if (screen) 1 else max([for (axis in 0 to 2) column_scale(world,axis)]));
    let hits=if (kind=='points' or kind=='sprite') [for (i in 0 to (len(positions)-1), let size=value(node.size,if (kind=='sprite') [1,1] else 1),
        let hit=if (kind=='points') point_hit(camera,positions[i],size*(if (geometry["point-sizes"]!=null) geometry["point-sizes"][indices[i]] else 1),world,screen,x,y)
            else nearest(triangle_hits(camera,face(camera,positions[i],size,value(node.center,[0.5,0.5]),world,screen),x,y)) where hit!=null)
        {*:hit,source_index:indices[i],primitive_index:i}]
    else [for (i in 0 to (if (kind=='line') len(positions)-2 else len(positions)/2-1), let start=if (kind=='line') i else i*2,
        let triangles=[*c.ribbon(camera,positions[start],positions[start+1],width,false,not screen),
            *(if (kind=='line' and start>0) bevel(camera,positions[start-1],positions[start],positions[start+1],width,not screen) else [])],
        let hit=nearest(triangle_hits(camera,triangles,x,y)) where hit!=null)
        {*:hit,source_index:indices[start],primitive_index:i}];
    let selected=if (node.visible==false or x<0 or y<0 or x>=camera.width or y>=camera.height) null else nearest(hits);
    if (selected==null) null else {*:selected,object:node.id}
    }
}
