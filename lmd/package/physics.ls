// S12.1.1v3/D7.2.4v4: scene constructors and bounded batch simulation are pure values.
import analysis: lambda.physics.analysis

pub fn world(options = {}) element => <physics *: options>
pub fn body(id, children, options = {}) element => <'rigid-body' id: id, type: 'dynamic', *: options, *children>
pub fn collider(id, shape, options = {}) element => <collider id: id, shape: shape, *: options>
// geometry definitions are pure data; the compiler copies referenced buffers into its owned world.
pub fn geometry(id, shape, options = {}) element => <'collision-geometry' id: id, version: 1, shape: shape, *: options>
pub fn joint(id, kind, body_a, body_b, options = {}) element => <joint id: id, type: kind, 'body-a': body_a, 'body-b': body_b, *: options>
pub fn simulate(scene, options = {}) map^ => lambda.sys.physics_simulate(scene, options)^
pub fn validate(scene) bool^ => lambda.sys.physics_validate(scene)^
pub fn query(scene, queries) array^ => lambda.sys.physics_query(scene, queries)^
// movement is a sampled-world query; the caller applies its copied target pose explicitly.
pub fn move(scene, body, displacement, options = {}) map^ {
    let results = query(scene, [{*: options, type: 'move', body: body, displacement: displacement}])^
    results[0]
}
pub fn bind_bone(node, body, options = {}) element^ {
    if (name(node) == 'bone') <bone *: node, *: options, 'physics-body': body, *content(node)>
    else raise error("physics.bind_bone: requires a bone")
}
pub fn ragdoll(id, bones, options = {}) element => <ragdoll id: id, bones: bones, *: options>
pub fn trajectory(trace, body) array => analysis.trajectory(trace, body)
pub fn chart_data(trace, options = {}) array => analysis.chart_data(trace, options)
pub fn energy(trace, body, gravity = [0.0, -9.81, 0.0]) array => analysis.energy(trace, body, gravity)
pub fn tracks(trace, body, target = null) array => analysis.tracks(trace, body, target)

pub fn attach(node, options) element^ {
    if (name(node) == 'mesh') <mesh *: node, physics: options, *content(node)>
    else if (name(node) == 'group') <group *: node, physics: options, *content(node)>
    else raise error("physics.attach: requires a mesh or group")
}
