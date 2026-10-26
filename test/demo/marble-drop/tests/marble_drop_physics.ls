import motion: ~~.mod_motion
import game: ~~.mod_model

let a = {x:0.0,y:0.0,kind:"funnel"}
let b = {x:0.0,y:160.0,kind:"bin"}
let fall = motion.start(a,b)
let half = motion.position(fall,fall.duration/2.0)
let quarter = motion.position(fall,fall.duration/4.0)
let tall = motion.start(a,{*:b,y:320.0})
let spring = motion.start({x:20.0,y:300.0,kind:"spring"},{x:340.0,y:170.0})
let mid = spring.duration/2.0
let p0 = motion.position(spring,mid-100.0)
let p1 = motion.position(spring,mid)
let p2 = motion.position(spring,mid+100.0)
let dropped = game.drop(game.new_game(),0,0)
let junction = reduce([dropped,*[for (i in 1 to int(ceil(dropped.active.motion.duration/game.STEP_MS))) i]],
    (st,i)=>game.tick(st))
{
    fall_duration:abs(fall.duration-1000.0*sqrt(2.0*160.0/(motion.GRAVITY*5.0/7.0))) < .001,
    accelerates_from_rest:abs(half.y-40.0) < .15 and abs(quarter.y-10.0) < .15,
    rolling_energy:abs(fall.speed*fall.speed-2.0*motion.GRAVITY*160.0*5.0/7.0) < .001,
    distance_controls_time:abs(tall.duration/fall.duration-sqrt(2.0)) < .001,
    momentum_carries:motion.start(a,b,100.0).duration < fall.duration,
    clamped_start:motion.position(fall,-100.0) == {x:0.0,y:0.0},
    clamped_finish:motion.position(fall,fall.duration+100.0) == {x:0.0,y:160.0},
    flight_horizontal_speed:abs((p2.x-p1.x)-(p1.x-p0.x)) < .001,
    flight_gravity:abs((p2.y-2.0*p1.y+p0.y)/.01-motion.GRAVITY) < .001,
    flight_rises:p0.y < spring.a.y and p1.y < spring.b.y,
    flight_lands:motion.position(spring,spring.duration) == {x:340.0,y:170.0},
    junction_keeps_speed:abs(junction.active.motion.samples[0].speed-dropped.active.motion.speed*.86) < .001,
    junction_keeps_time:junction.active.tick > 0.0 and junction.active.tick < 1.0,
    mass_independent:game.drop(game.new_game(),0,6).active.motion.duration == dropped.active.motion.duration
}
