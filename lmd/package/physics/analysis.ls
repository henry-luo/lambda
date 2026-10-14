import animation: lambda.scene3d.animation

pub fn trajectory(trace, body) array => [for (frame in trace.frames)
    for (sample in frame.bodies where sample.id == body) {*: sample, tick: frame.tick, time: frame.time}]

pub fn chart_data(trace, options = {}) array => [for (frame in trace.frames)
    for (sample in frame.bodies where options.body == null or sample.id == options.body)
        {body: sample.id, tick: frame.tick, time: frame.time, height: sample.position[1],
         x: sample.position[0], y: sample.position[1], z: sample.position[2],
         speed: sqrt(sum(sample.velocity |> ~ * ~)), mass: sample.mass, sleeping: sample.sleeping}]

// translational kinetic and uniform-gravity potential terms; rotational/contact work is not included.
pub fn energy(trace, body, gravity = [0.0, -9.81, 0.0]) array => [for (sample in trajectory(trace, body))
    {tick: sample.tick, time: sample.time,
     kinetic: 0.5 * sample.mass * sum(sample.velocity |> ~ * ~),
     potential: -sample.mass * sum([for (i in 0 to 2) gravity[i] * sample.centre_of_mass[i]])}]

pub fn tracks(trace, body, target = null) array {
    let states = trajectory(trace, body)
    let label = if (target == null) body else target;
    if (len(states) == 0) [] else [animation.track(label ++ ".position", states |> ~.time,
        [for (sample in states) *sample.position], 'vector'),
     animation.track(label ++ ".quaternion", states |> ~.time,
        [for (sample in states) *sample.quaternion], 'quaternion')]
}
