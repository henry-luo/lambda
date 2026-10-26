// Rail energy and ballistic spring flights use the same geometry as the drawing.
pub let STEP_MS = 40.0
pub let GRAVITY = 800.0
let ROLLING = 5.0/7.0
let SAMPLES = 32

fn flight(a,b) {
    let vy = -sqrt(2.0*GRAVITY*(a.y-min(a.y,b.y)+100.0))
    let seconds = (-vy+sqrt(vy*vy+2.0*GRAVITY*(b.y-a.y)))/GRAVITY
    {seconds:seconds,vx:(b.x-a.x)/seconds,vy:vy}
}
fn controls(a,b) {
    if (a.kind == "spring") {
        let f = flight(a,b)
        {x1:a.x+(b.x-a.x)/3.0,y1:a.y+f.vy*f.seconds/3.0,
         x2:a.x+2.0*(b.x-a.x)/3.0,y2:b.y-(f.vy+GRAVITY*f.seconds)*f.seconds/3.0}
    } else { {x1:a.x,y1:(a.y+b.y)/2.0,x2:b.x,y2:(a.y+b.y)/2.0} }
}
pub fn point(a,b,t) {
    let u = 1.0-t
    let c = controls(a,b)
    {x:u*u*u*a.x+3.0*u*u*t*c.x1+3.0*u*t*t*c.x2+t*t*t*b.x,
     y:u*u*u*a.y+3.0*u*u*t*c.y1+3.0*u*t*t*c.y2+t*t*t*b.y}
}
pub fn rail(a,b) {
    let c = controls(a,b)
    "M" ++ string(a.x) ++ " " ++ string(a.y) ++ " C" ++ string(c.x1) ++ " " ++ string(c.y1) ++ " " ++
        string(c.x2) ++ " " ++ string(c.y2) ++ " " ++ string(b.x) ++ " " ++ string(b.y)
}
fn sample(a,b,speed,table,index) {
    let p = point(a,b,index/SAMPLES)
    let prev = table[len(table)-1]
    let dx = p.x-prev.x
    let dy = p.y-prev.y
    let velocity = sqrt(speed*speed+2.0*GRAVITY*ROLLING*max(0.0,p.y-a.y))
    let ms = prev.ms+2000.0*sqrt(dx*dx+dy*dy)/(prev.speed+velocity);
    [*table,{*:p,t:index/SAMPLES,ms:ms,speed:velocity}]
}
pub fn start(a,b,speed = 0.0) {
    if (a.kind == "spring") {
        let f = flight(a,b)
        let vy = f.vy+GRAVITY*f.seconds
        {kind:"flight",a:a,b:b,duration:f.seconds*1000.0,speed:sqrt(f.vx*f.vx+vy*vy),samples:[]}
    } else {
        // ds / mean velocity integrates gravity along the sampled rail, including the first step from rest.
        let table = reduce([[{x:a.x,y:a.y,t:0.0,ms:0.0,speed:speed}],*[for (i in 1 to SAMPLES) i]],
            (values,i)=>sample(a,b,speed,values,i))
        let end = table[len(table)-1]
        {kind:"rail",a:a,b:b,duration:end.ms,speed:end.speed,samples:table}
    }
}
fn upper(table,ms,lo,hi) int {
    if (hi-lo <= 1) hi
    else {
        let middle = int(floor((lo+hi)/2.0))
        if (ms < table[middle].ms) upper(table,ms,lo,middle)
        else upper(table,ms,middle,hi)
    }
}
pub fn position(track,elapsed) {
    let ms = min(track.duration,max(0.0,elapsed))
    if (track.kind == "flight") point(track.a,track.b,ms/track.duration)
    else {
        let index = upper(track.samples,ms,0,len(track.samples)-1)
        let a = track.samples[index-1]
        let b = track.samples[index]
        let u = (ms-a.ms)/(b.ms-a.ms)
        let fraction = (2.0*a.speed*u+(b.speed-a.speed)*u*u)/(a.speed+b.speed)
        point(track.a,track.b,a.t+(b.t-a.t)*fraction)
    }
}
