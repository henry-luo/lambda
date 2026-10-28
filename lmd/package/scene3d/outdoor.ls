// S12.1.1v3 / D7.2.4v4: outdoor materials and motion are pure descriptions.
import animation: .animation
import t: .transform

pub fn fog(kind='exp2',options={}) element => <fog type:kind,color:"#d5dedb",*:options>
pub fn foliage(options={}) element => <material type:'standard',color:"#6a8e3d",side:'double',
    roughness:0.8,foliage:0.6,'foliage-thickness':0.3,*:options>
// depth is a finite optical path, supplied from the world's water state rather than inferred from opacity.
pub fn water(depth=0.5,options={}) element => <material type:'physical',color:"#ffffff",side:'double',
    roughness:0.12,metalness:0.0,transmission:1.0,ior:1.333,thickness:depth,
    'attenuation-color':"#438c88",'attenuation-distance':3.0,
    'water-amplitude':0.025,'water-frequency':2.0,*:options>

fn cycle(material_id,kind,property,duration,options) element^ {
    let id=string(material_id)
    let checked=if (len(id)==0 or len(id)>110 or contains(id,".") or not t.numeric(duration) or duration<=0)
        raise error("scene3d.outdoor: cycle needs a material ID without dots and positive duration")
    animation.clip(id++"-"++kind,[animation.track(id++"."++property,
        [0.0,duration],[0.0,2.0*math.pi])],{autoplay:true,*:options,duration:duration})
}
pub fn wind(material_id,duration=8.0,options={}) element^ => cycle(material_id,"wind","wind-phase",duration,options)^
pub fn flow(material_id,duration=8.0,options={}) element^ => cycle(material_id,"flow","water-phase",duration,options)^
