// shared retained orbit controls and instanced scene construction (S12.1.3 / D9.5.6).
import drawing: lambda.procart.scene
import s: lambda.scene3d
import camera: lambda.scene3d.camera
import viewer: lambda.scene3d.viewer
import geometry: ~~.scene3d._geometry
import solids: .mod_plant_solids

pub fn pose(position=[6.8,4.8,8.2]) map^ => camera.prepare({position:position,target:[0.0,2.0,0.0],fov:38.0})

// batch shared shapes across a composition while retaining semantic category selectors.
pub fn meshes(parts,options={}) array^ => drawing.meshes({parts:[for (i,p in parts)
    {id:string(i),geometry:p.shape,matrix:solids.matrix(p),color:p.color,opacity:1.0,
     kind:if (p.category != null) p.category else "",role:p.role}]},options)

pub fn viewport(parts,resources,options) element^ {
    let box = solids.bounds(if (options.fit != null) options.fit else parts)
    let center = [(box.lo[0]+box.hi[0])/2.0,box.lo[1],(box.lo[2]+box.hi[2])/2.0]
    let lens = options.camera;
    s.scene([
        s.camera({id:options.camera_id,position:lens.position,target:lens.target,fov:lens.fov}),
        s.light('ambient',{color:"#e3ecd4",intensity:0.65}),
        s.light('directional',{position:[-4.0,9.0,6.0],color:"#fff1d1",intensity:0.8}),
        s.light('directional',{position:[5.0,4.0,-5.0],color:"#c4d9d5",intensity:0.45}),
        s.resources([geometry.sphere("round",10,16),geometry.sphere("plinth",8,48),*resources]),
        <mesh geometry:"plinth",position:[0.0,-0.09,0.0],scale:[2.4,0.09,2.4],s.material('lambert',{color:"#d8d6bb"})>,
        s.group([s.group(meshes(parts),{position:[for (v in center) -v]})],{id:options.model_id,scale:[box.scale,box.scale,box.scale]})
    ],{id:options.id,camera:options.camera_id,width:760,height:540,viewBox:"0 0 760 540",background:"#f4f3e7",
        tabindex:"0",role:"img",'aria-label':options.label,style:"display:block;width:100%;height:auto;touch-action:none"})
}

// Shared controls are used by the demo gallery and procedural art views.
pub fn controls(labels={}) element => viewer.controls(labels)
pub fn source(scene,lens,initial=null,labels={},chrome=true) element^ => viewer.source(scene,lens,initial,labels,chrome)^
pub fn interactive(scene,lens,initial=null,labels={},chrome=true) element^ => viewer.interactive(scene,lens,initial,labels,chrome)^
pub pn snapshot(owner,id,fallback) { return viewer.snapshot(owner,id,fallback)^ }
pub pn set_camera(owner,id,next) { return viewer.set_camera(owner,id,next)^ }
pub pn command(owner,id,action) { return viewer.command(owner,id,action)^ }
