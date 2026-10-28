// PG15: environment is saved data; solar and plant responses are pure samples (S12.1.1v3).
import c: ._common
import rng: .rng
import t: lambda.scene3d.transform
import s: lambda.scene3d
import color: lambda.color

pub let WEATHER = ["sunny","cloudy","rainy","foggy","windy"]
pub let PRESETS = {sunny:{cloud:0.0,rain:0.0,fog:0.0,wind_speed:0.0,humidity:0.55},
    cloudy:{cloud:0.8,rain:0.0,fog:0.0,wind_speed:1.5,humidity:0.7},
    rainy:{cloud:0.95,rain:1.0,fog:0.15,wind_speed:3.5,humidity:0.95},
    foggy:{cloud:0.65,rain:0.0,fog:1.0,wind_speed:0.3,humidity:0.98},
    windy:{cloud:0.25,rain:0.0,fog:0.0,wind_speed:7.0,humidity:0.55}}
let DEFAULTS = {latitude:45.0,altitude:120.0,day:172,time:14.0,weather:"sunny",wind_direction:70.0,lighting:{}}
let FLEX = {tree:0.12,grass:1.0,reed:0.75,sunflower:0.45,lavender:0.65,'water-lily':0.04}
fn clamp(x,lo=0.0,hi=1.0) float => max(lo,min(hi,x))
fn radians(x) float => x*math.pi/180.0

fn blend(a,b,weight) array => color.parse(a)^*(1.0-weight)+color.parse(b)^*weight

pub fn normalize(raw=null) map^ {
    let checked=c.require(raw == null or raw is map or (raw is element and name(raw)=='environment' and len(content(raw))==0),"invalid environment element")^
    let values=if (raw == null) {} else if (raw is element) map(raw) else raw
    let fields=c.require(c.fields(values,["latitude","altitude","day","time","weather","humidity","wind_speed","wind_direction","lighting"]),"unknown environment attribute")^
    let cfg={*:DEFAULTS,*:values}
    let weather=c.require(contains(WEATHER,cfg.weather),"unknown environment weather")^
    let preset=PRESETS[cfg.weather]
    let result={humidity:preset.humidity,wind_speed:preset.wind_speed,*:cfg}
    let checked_range=c.require(c.bounded(result.latitude,-90.0,90.0) and c.bounded(result.altitude,-500.0,9000.0) and
        c.bounded(result.day,1,365,true) and c.bounded(result.time,0.0,24.0) and c.bounded(result.humidity,0.0,1.0) and
        c.bounded(result.wind_speed,0.0,30.0) and c.bounded(result.wind_direction,0.0,360.0),"environment value outside range")^
    let light_fields=c.require(cfg.lighting is map and c.fields(cfg.lighting,["sun","sky","ambient"]),"unknown environment lighting field")^
    let lighting={sun:1.0,sky:1.0,ambient:1.0,*:cfg.lighting}
    let lights=c.require(all([for (key,val in lighting) c.bounded(val,0.0,4.0)]),"invalid environment lighting multiplier")^;
    {*:result,lighting:lighting}
}
pub fn from_document(doc) map?^ {
    let nodes=[for (n in content(doc) where n is element and name(n)=='environment') n]
    let checked=c.require(len(nodes)<=1,"only one global environment is allowed")^;
    if (len(nodes)==0) null else normalize(nodes[0])^
}
pub fn description(cfg) element^ => <environment *:normalize(cfg)^>

// east +X, up +Y, north -Z; local solar noon has hour angle zero.
pub fn sample(raw=null) map^ {
    let cfg=normalize(raw)^,latitude=radians(cfg.latitude)
    let declination=radians(23.44)*sin(2.0*math.pi*(284.0+float(cfg.day))/365.0)
    let hour=radians((cfg.time-12.0)*15.0)
    let direction=[0.0-cos(declination)*sin(hour),sin(latitude)*sin(declination)+cos(latitude)*cos(declination)*cos(hour),
        sin(latitude)*cos(declination)*cos(hour)-cos(latitude)*sin(declination)]
    let elevation=math.asin(clamp(direction[1],-1.0,1.0))*180.0/math.pi
    let daylight=clamp((elevation+6.0)/14.0),weather=PRESETS[cfg.weather]
    let direct=clamp(direction[1])* (1.0-0.85*weather.cloud)*(1.0-0.65*weather.fog)*(1.0+cfg.altitude/40000.0)
    let sky=blend("#142337",if (weather.fog>0.5) "#c8d3cd" else if (weather.cloud>0.5) "#a8b9bc" else "#dfe9d2",daylight)
    let sun_color=blend("#f0ba83","#fff3d2",clamp(elevation/35.0));
    {config:cfg,sun:{direction:direction,elevation:elevation,daylight:daylight},weather:weather,
     background:color.hex(sky),sun_color:color.hex(sun_color),
     lighting:{sun:direct*1.6*cfg.lighting.sun,sky:(0.12+daylight*0.65)*cfg.lighting.sky,
        ambient:(0.06+daylight*0.22+weather.fog*0.16*daylight)*cfg.lighting.ambient},
     wind:{speed:cfg.wind_speed,direction:[sin(radians(cfg.wind_direction)),0.0,0.0-cos(radians(cfg.wind_direction))]}}
}
pub fn lights(env,extent=18.0) array => [
    s.light('hemisphere',{id:"environment-sky",color:env.background,'ground-color':"#647454",intensity:env.lighting.sky}),
    s.light('directional',{id:"environment-sun",position:[for (x in env.sun.direction) x*30.0],color:env.sun_color,intensity:env.lighting.sun,
        'cast-shadow':true,'shadow-map-size':2048,'shadow-extent':extent,'shadow-intensity':0.4,'shadow-softness':2.0}),
    s.light('ambient',{id:"environment-ambient",color:"#d9e4e8",intensity:env.lighting.ambient})]

pub fn response(species,env) map {
    let lily=species == "water-lily"
    let opening=if (lily) clamp((env.sun.elevation-2.0)/22.0)*(1.0-0.55*env.weather.rain)
        else if (contains(["daisy","poppy"],species)) 0.25+0.75*env.sun.daylight else 1.0;
    {openness:opening,droop:(1.0-env.config.humidity)*0.18,flexibility:c.value(FLEX[species],0.6),
     direction:if (species=="sunflower") (if (env.sun.elevation>0.0) env.sun.direction else [1.0,0.15,0.0]) else [0.0,1.0,0.0]}
}
pub fn head(direction,frame=c.identity()) array^ {
    let inverse=t.inverse(frame)^,origin=t.point(inverse,[0.0,0.0,0.0])^,end=t.point(inverse,direction)^
    let d=[for (i in 0 to 2) end[i]-origin[i]],length=sqrt(sum(d*d)),unit=d/length
    let q=if (unit[1]<-0.999999) [1.0,0.0,0.0,0.0] else t.quaternion_normalize([unit[2],0.0,0.0-unit[0],1.0+unit[1]]);
    t.compose([0.0,0.0,0.0],q,[1.0,1.0,1.0])
}
pub fn wind(env,key,seconds=0.0,flexibility=1.0) array {
    let phase=rng.between(rng.derive(rng.key(key),"wind-phase"),0.0,2.0*math.pi)
    let wave=0.5+0.35*sin(2.0*math.pi*seconds/4.0+phase)+0.15*sin(2.0*math.pi*seconds/ (8.0/3.0)+phase*2.0)
    let angle=min(0.32,env.wind.speed*0.018)*flexibility*wave,d=env.wind.direction;
    [d[2]*sin(angle/2.0),0.0,0.0-d[0]*sin(angle/2.0),cos(angle/2.0)]
}
pub fn wind_track(id,env,key,flexibility=1.0) element => s.track(id++".quaternion",[for (i in 0 to 32) float(i)/4.0],
    [for (i in 0 to 32) *wind(env,key,float(i)/4.0,flexibility)],'quaternion')

// translate existing world matrices around a rooted group's pivot once, before animation.
pub fn rooted(parts,pivot) array {
    let offset=t.matrix([for (x in pivot) 0.0-x],[0.0,0.0,0.0],[1.0,1.0,1.0])^;
    [for (part in parts) {*:part,matrix:t.multiply(offset,part.matrix)^}]
}
