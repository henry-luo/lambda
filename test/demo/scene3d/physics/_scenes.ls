// S12.1.1v2: these descriptions are shared by live scenes and deterministic batch checks.
import physics: lambda.physics
import geometry: ~~._geometry

pub let palette = ["#60a5fa", "#34d399", "#fbbf24", "#fb7185", "#a78bfa"]

pub fn visual(part) element =>
    <mesh position: (part.position or [0.0,0.0,0.0]), rotation: (part.rotation or [0.0,0.0,0.0]),
        *:(if (part.shape == 'sphere') {geometry:"ball-unit",scale:[part.radius,part.radius,part.radius]} else {}),
        *[if (part.shape != 'sphere') <geometry type:'box',size:part.size>,
          <material type:'lambert',color:(part.color or "#94a3b8"),opacity:(part.opacity or 1.0)>]>

// one part list supplies both collision and presentation dimensions.
pub fn body(id, parts, options = {}) element => physics.body(id,
    [for (i,part in parts) *[
        physics.collider(id++"-shape-"++string(i),part.shape,
            {position:[0.0,0.0,0.0],density:1.0,friction:0.5,restitution:0.2,*:part}),
        if (part.hidden != true) visual(part)]], options)

fn box(size, color, position = [0.0,0.0,0.0]) => {shape:'box',size:size,color:color,position:position}
fn ball(radius, color, position = [0.0,0.0,0.0]) => {shape:'sphere',radius:radius,color:color,position:position}

pub fn camera(kind) map =>
    if (kind == "domino") {position:[11.0,10.0,14.0],target:[0.0,0.5,0.0],fov:42.0}
    else if (kind == "salvage") {position:[11.5,8.0,13.0],target:[0.0,3.1,0.0],fov:43.0}
    else if (kind == "sorter") {position:[11.0,10.0,13.0],target:[0.0,0.2,0.0],fov:43.0}
    else {position:[8.2,5.8,11.5],target:[0.0,1.6,0.0],fov:42.0}

fn viewport(kind, objects, config) element => <scene3d id:"world",camera:"main",width:1040,height:500,
    background:"#101b2b",tabindex:"0",role:"img",'aria-label':kind++" physics scene",
    'data-demo':kind,'data-camera':format(camera(kind),'json'),'data-playing':string(config.state == 'playing'),
    *[physics.world({timestep:1.0/120.0,'solver-iterations':18,*:config}),
      <camera id:"main",type:'perspective',*:camera(kind)>,
      <light type:'ambient',intensity:0.48,color:"#c5d6ff">,
      <light type:'directional',position:[-6.0,10.0,8.0],intensity:0.85,color:"#fff1dd">,
      <light type:'directional',position:[7.0,4.0,-6.0],intensity:0.25,color:"#82aaff">,
      <resources geometry.sphere("ball-unit",12,20)>, *objects]>

pub fn domino_position(i) array => [-7.2+0.46*i,0.9,1.15*sin(i*0.19)]
pub fn domino_commands() array => [{type:"impulse",body:"tile-0",value:[0.34,0.0,0.16],point:[-7.2,1.58,0.0]}]
pub fn domino() element => viewport("domino",[
    body("table",[box([17.0,0.3,6.5],"#34445b")],{type:'fixed',position:[0.0,-0.15,0.0]}),
    for (i in 0 to 31)
        body("tile-"++string(i),[{*:box([0.16,1.8,0.75],palette[int(i/7)%5]),density:1.0,friction:0.65,restitution:0.05}],
            {position:domino_position(i),rotation:[0.0,0.0-math.atan2(1.15*0.19*cos(i*0.19),0.46),0.0]}),
    for (i in 0 to 31) visual(box([0.23,0.012,0.85],"#526781",[domino_position(i)[0],0.006,domino_position(i)[2]])),
    visual(box([16.0,0.014,0.025],"#65e8cf",[0.0,0.009,-2.6])),
    visual(box([16.0,0.014,0.025],"#65e8cf",[0.0,0.009,2.6]))
],{state:'paused'})

fn assembly_parts(i) array =>
    if (i%3 == 0) [
        {*:ball(0.42,palette[i%5],[-0.65,0.0,0.0]),density:3.0},
        {*:ball(0.31,"#d5e4f5",[0.65,0.0,0.0]),density:1.0},
        box([1.3,0.16,0.16],"#62768e")]
    else if (i%3 == 1) [
        box([0.48,1.35,0.48],palette[i%5],[-0.4,0.0,0.0]),
        box([1.25,0.42,0.48],"#d5e4f5",[0.05,-0.47,0.0])]
    else [box([1.45,0.35,0.35],palette[i%5]),box([0.35,1.45,0.35],"#d5e4f5"),
          box([0.35,0.35,1.45],palette[(i+2)%5])]

pub fn salvage_commands() array => [for (i in 0 to 8)
    {type:"impulse",body:"cargo-"++string(i),local:true,value:[0.3*(i%3-1),0.6,0.5],point:[0.6,0.35,0.0]}]
pub fn salvage() element => viewport("salvage",[
    body("chamber",[
        box([10.0,0.3,7.0],"#283950",[0.0,-0.15,0.0]),
        {*:box([10.0,0.25,7.0],"#283950",[0.0,7.1,0.0]),hidden:true},
        {*:box([0.25,7.0,7.0],"#283950",[-5.0,3.5,0.0]),hidden:true},
        {*:box([0.25,7.0,7.0],"#283950",[5.0,3.5,0.0]),hidden:true},
        {*:box([10.0,7.0,0.25],"#283950",[0.0,3.5,3.5]),hidden:true},
        {*:box([10.0,7.0,0.25],"#1b2b42",[0.0,3.5,-3.5]),opacity:0.55}
    ],{type:'fixed'}),
    for (x in [-4.9,4.9]) for (z in [-3.4,3.4]) visual(box([0.035,7.0,0.035],"#52708c",[x,3.5,z])),
    for (y in [0.02,7.0]) *[
        for (z in [-3.4,3.4]) visual(box([9.8,0.035,0.035],"#52708c",[0.0,y,z])),
        for (x in [-4.9,4.9]) visual(box([0.035,0.035,6.8],"#52708c",[x,y,0.0]))],
    for (i in 0 to 8) body("cargo-"++string(i),assembly_parts(i),
        {position:[-2.8+2.8*(i%3),1.65+1.65*int(i/3),0.8*sin(i*2.3)],
         rotation:[0.2*i,0.4*i,0.1*i],
         'linear-velocity':[0.25*cos(i*1.7),0.12*sin(i*2.1),0.2*sin(i*1.3)],
         'angular-velocity':[0.25+0.08*(i%3),0.18,0.2],
         'linear-damping':0.015,'angular-damping':0.01})
],{state:'playing',gravity:[0.0,0.0,0.0]})

pub fn sorter() element => viewport("sorter",[
    body("tray",[
        box([14.0,0.25,7.2],"#2d4056"),
        box([14.0,1.35,0.15],"#526981",[0.0,0.65,-3.6]),
        {*:box([14.0,1.35,0.15],"#526981",[0.0,0.65,3.6]),opacity:0.25},
        {*:box([0.15,1.35,7.2],"#526981",[6.95,0.65,0.0]),opacity:0.25},
        box([0.15,1.35,7.2],"#526981",[-6.95,0.65,0.0]),
        for (z in [-1.15,1.15]) {*:box([2.6,1.35,0.12],"#7790aa",[5.65,0.65,z]),opacity:0.4}
    ],{type:'fixed',position:[0.0,-0.2,0.0],rotation:[0.0,0.0,-0.12]}),
    body("paddle",[box([0.22,0.65,4.4],"#fbbf24")],
        {type:'kinematic-velocity',position:[0.0,0.25,0.0],'angular-velocity':[0.0,1.5,0.0]}),
    visual(ball(0.27,"#fef3c7",[0.0,0.75,0.0])),
    for (i in 0 to 2) *[
        body("bin-"++string(i),[{shape:'box',size:[1.9,1.5,1.9],sensor:true,hidden:true}],
            {type:'fixed',position:[5.8,-0.15,-2.35+2.35*i]}),
        visual(box([1.8,0.025,1.9],palette[i],[5.8,-0.75,-2.35+2.35*i]))],
    for (i in 0 to 17) body("marble-"++string(i),
        [{*:ball(0.27,palette[i%5]),friction:0.12,restitution:0.35}],
        {position:[-5.8+0.65*(i%3),1.3+0.65*int(i/9),-2.5+0.85*(int(i/3)%6)]})
],{state:'playing'})

pub let bounce_coefficients = [0.0,0.35,0.6,0.8,0.95]
pub let bounce_ids = [for (i in 0 to 4) "ball-"++string(i)]
pub fn bounce() element => viewport("bounce",[
    body("platform",[{*:box([10.0,0.3,4.0],"#34445b"),friction:0.5,restitution:0.0}],
        {type:'fixed',position:[0.0,-0.15,0.0]}),
    for (i in 0 to 4) *[
        body(bounce_ids[i],[{*:ball(0.38,palette[i]),restitution:bounce_coefficients[i],friction:0.2}],
            {position:[-3.4+1.7*i,3.8,0.0]}),
        visual(box([0.9,0.012,0.9],palette[i],[-3.4+1.7*i,0.006,0.0]))],
    for (y in [1.0,2.0,3.0,4.0]) visual(box([9.1,0.016,0.016],"#53708b",[0.0,y,-1.7]))
],{state:'playing'})
