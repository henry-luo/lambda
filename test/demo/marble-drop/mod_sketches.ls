// crop the original sheet in CSS, so each study can move without copying its pixels
let SHEET = "assets/sketches/engineering-studies.png"
pub let CATALOG = [
    {id:"gears",src:SHEET,sw:1536,sh:1024,x:30,y:50,w:580,h:465},
    {id:"wing",src:SHEET,sw:1536,sh:1024,x:845,y:55,w:670,h:410},
    {id:"pulley",src:SHEET,sw:1536,sh:1024,x:70,y:525,w:400,h:460},
    {id:"escapement",src:SHEET,sw:1536,sh:1024,x:1200,y:500,w:315,h:400},
    {id:"aerial-screw",src:"assets/sketches/aerial-screw.png",sw:1254,sh:1254,x:0,y:0,w:1254,h:1254},
    {id:"arched-bridge",src:"assets/sketches/arched-bridge.png",sw:1254,sh:1254,x:0,y:0,w:1254,h:1254},
    {id:"water-lifter",src:"assets/sketches/water-lifter.png",sw:1254,sh:1254,x:0,y:0,w:1254,h:1254}
]
let ZONES = [{x:132,y:142},{x:588,y:142},{x:132,y:332},{x:588,y:332}]

fn place(available,slot,seed,placed:array) array {
    if (slot == len(ZONES)) placed
    else {
        let choice, s1 = math.random(seed)
        let dx, s2 = math.random(s1)
        let dy, s3 = math.random(s2)
        let scale, s4 = math.random(s3)
        let angle, s5 = math.random(s4)
        let ink, s6 = math.random(s5)
        let index = int(floor(choice*len(available)))
        let study = available[index]
        let zone = ZONES[slot]
        let width = min(190,165*study.w/study.h)*(.78+scale*.27)
        let height = width*study.h/study.w
        let item = {study:study,x:zone.x+(dx-.5)*24-width/2,y:zone.y+(dy-.5)*24-height/2,
            w:width,h:height,angle:(angle-.5)*32,opacity:.25+ink*.1};
        place([for (i,s in available where i != index) s],slot+1,s6,[*placed,item])
    }
}

// level seeds give each folio a different composition without reshuffling on redraw or undo
pub fn layout(stage) => place(CATALOG,0,5107+stage*92821,[])

pub fn underlay(stage,composition) => <div class:"sketch-underlay", 'aria-hidden':"true",
    'data-sketch-level':string(stage+1),
    for (i,item in composition) <div id:"sketch-" ++ string(i), class:"notebook-sketch",
        'data-study':item.study.id,
        style:"left:" ++ string(item.x/720*100) ++ "%;top:" ++ string(item.y/518*100) ++
            "%;width:" ++ string(item.w/720*100) ++ "%;height:" ++ string(item.h/518*100) ++
            "%;transform:rotate(" ++ string(item.angle) ++ "deg);opacity:" ++ string(item.opacity),
        <img src:item.study.src, alt:"", draggable:"false",
            style:"left:" ++ string(-item.study.x/item.study.w*100) ++
                "%;top:" ++ string(-item.study.y/item.study.h*100) ++
                "%;width:" ++ string(item.study.sw/item.study.w*100) ++
                "%;height:" ++ string(item.study.sh/item.study.h*100) ++ "%">>>
