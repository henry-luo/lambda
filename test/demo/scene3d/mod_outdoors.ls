// S12.1.1v3: the existing procedural Meadow supplies geometry; this adapter studies outdoor rendering.
import s: lambda.scene3d
import o: lambda.scene3d.outdoor
import meadow: ~~.procart.mod_meadow

pub fn home() map => {*:meadow.home(),position:[23.0,12.0,25.0],target:[0.0,1.3,0.0],fov:43.0}
fn decorate(n,path,organ,options) element^ {
    let tag=name(n)
    let role=if (tag=='mesh' and n.id=="pond-water") "water"
        else if (map(n)["data-organ"]!=null) string(map(n)["data-organ"]) else organ
    let children=[for (i,child in content(n)) decorate(child,path++"-"++string(i),role,options)^]
    let attrs=map(n)
    if (tag=='material') {
        if (role=="water") o.water(0.4,{id:"outdoor-water",roughness:0.14})
        else if (role=="leaf" or role=="grass") o.foliage({*:attrs,id:"outdoor-leaf-"++path,type:'standard',
            foliage:0.65,'wind-strength':if (options.wind and role=="grass") 0.3 else 0.0,'wind-height':1.0})
        else if (n.type=='lambert') <material *:{*:attrs,type:'standard',roughness:0.9}>
        else n
    } else if (tag=='mesh') <mesh *:{*:attrs,*:(if (role=="water") {'cast-shadow':false} else {})},*children>
    else if (tag=='light') <light *:{*:attrs,*:(if (n.id=="environment-sun")
        {'shadow-cascades':options.cascades,'shadow-distance':80.0,'shadow-map-size':1024,'shadow-intensity':0.75} else {})}>
    else if (tag=='camera') <camera *:attrs>
    else if (tag=='resources') <resources *:attrs,*children>
    else if (tag=='group') <group *:attrs,*children>
    else if (tag=='scene3d') <scene3d *:{*:attrs,id:"outdoor-scene",environment:"outdoor-sky",
        'tone-mapping':'aces',exposure:1.1,background:"#bac9c9",width:1100,height:680,
        style:"display:block;width:100%;height:570px;touch-action:none"},*children;
        s.environment('sky',{id:"outdoor-sky",'sun-direction':[0.5,0.4,0.3],'sun-intensity':12.0})^;
        *(if (options.mist) [o.fog('height',{color:"#bac9c9",density:0.012,height:0.0,falloff:0.12})] else [])>
    else n
}
fn tracks(n) array => [
    *(if (name(n)=='material' and n.id!=null and n["wind-strength"]!=null and n["wind-strength"]>0.0)
        [s.track(n.id++".wind-phase",[0.0,8.0],[0.0,2.0*math.pi])] else []),
    *(if (name(n)=='material' and n.id=="outdoor-water")
        [s.track(n.id++".water-phase",[0.0,8.0],[0.0,2.0*math.pi])] else []),
    *[for (child in content(n)) *tracks(child)]]
pub fn viewport(model,options,lens=home()) element^ {
    let base=decorate(meadow.viewport(model,{},false,lens)^,"root","",options)^
    let motion=tracks(base);
    <scene3d *:map(base),*content(base);s.clip("outdoor-motion",motion,{duration:8.0,autoplay:options.motion})>
}
