// Courier runs: collect a parcel at a curbside beacon, then deliver it across town before the clock
// runs out. Each delivery pays a base fare plus the seconds to spare, multiplied by the streak, and
// the next pickup appears at once. A late delivery ends the run.
import u: .mod_util
import city: .mod_city
import rng: lambda.procart.rng

pub let REACH = 6.0                 // how close the car must come to a beacon
let KEY = rng.key([city.SEED, "courier"])
// curbside stops at the middle of every block side, on the sidewalk edge
pub let STOPS = [for (b in city.BLOCKS, p in [
        {x: (b.x0 + b.x1) / 2.0, z: b.z0 + city.ROAD + 1.2}, {x: (b.x0 + b.x1) / 2.0, z: b.z1 - city.ROAD - 1.2},
        {x: b.x0 + city.ROAD + 1.2, z: (b.z0 + b.z1) / 2.0}, {x: b.x1 - city.ROAD - 1.2, z: (b.z0 + b.z1) / 2.0}]
    where b.kind != "S") {x: p.x, z: p.z, name: city.street(p.x, p.z)}]

pub fn initial() => {phase: "idle", target: null, deadline: 0.0, limit: 0.0, cash: 0, streak: 0, delivered: 0,
    runs: 0, damage_at_pickup: 0.0, message: null}
// a stop at least `far` metres from the car, chosen by a key that changes with every leg
fn pick(x, z, far, salt) {
    let options = [for (s in STOPS where u.dist(s.x, s.z, x, z) >= far) s];
    options[rng.below(rng.derive(KEY, salt), len(options))]
}
pub fn begin(m, car) {
    let target = pick(car.x, car.z, 110.0, ["pickup", m.runs, m.delivered]);
    {*: m, phase: "pickup", target: target, deadline: 0.0, streak: 0, runs: m.runs + 1,
     message: "Pick up the parcel on " ++ target.name}
}
pub fn stop(m) => {*: m, phase: "idle", target: null, streak: 0, message: "Courier run ended"}
// the allowance grows with the city-block distance between the two stops
pub fn allowance(a, b) => round((abs(a.x - b.x) + abs(a.z - b.z)) / 11.0 + 14.0)
pub fn reward(m, left) => int(round((60.0 + 6.0 * max(0.0, left)) * (1.0 + 0.25 * m.streak))) ^ { 0 }

pub fn step(m, car, t) {
    let near = m.target != null and u.dist(car.x, car.z, m.target.x, m.target.z) < REACH;
    if (m.phase == "pickup" and near) {
        let dropoff = pick(car.x, car.z, 190.0, ["dropoff", m.runs, m.delivered])
        let limit = allowance(m.target, dropoff);
        {*: m, phase: "deliver", target: dropoff, deadline: t + limit, limit: limit, damage_at_pickup: car.damage,
         message: "Parcel aboard: deliver to " ++ dropoff.name ++ " in " ++ string(int(limit) ^ { 0 }) ++ " s"}
    }
    else if (m.phase == "deliver" and near) {
        let pay = reward(m, m.deadline - t)
        let next = pick(car.x, car.z, 110.0, ["pickup", m.runs, m.delivered + 1]);
        {*: m, phase: "pickup", target: next, cash: m.cash + pay, streak: m.streak + 1, delivered: m.delivered + 1,
         message: "Delivered! +$" ++ string(pay) ++ (if (m.streak > 0) "  ×" ++ string(m.streak + 1) ++ " streak" else "")}
    }
    else if (m.phase == "deliver" and t > m.deadline) {*: m, phase: "idle", target: null, streak: 0, message: "Too late: the parcel run is over"}
    else {*: m, message: null}
}
pub fn time_left(m, t) => if (m.phase == "deliver") max(0.0, m.deadline - t) else 0.0
