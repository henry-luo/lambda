// S12.1.1v2/D7.2.4: scene constructors and bounded batch simulation are pure values.
import analysis: lambda.physics.analysis

pub fn world(options = {}) element => <physics *: options>
pub fn body(id, children, options = {}) element => <'rigid-body' id: id, type: 'dynamic', *: options, *children>
pub fn collider(id, shape, options = {}) element => <collider id: id, shape: shape, *: options>
pub fn simulate(scene, options = {}) map^ => lambda.sys.physics_simulate(scene, options)^
pub fn validate(scene) bool^ => lambda.sys.physics_validate(scene)^
pub fn query(scene, queries) array^ => lambda.sys.physics_query(scene, queries)^
pub fn trajectory(trace, body) array => analysis.trajectory(trace, body)
pub fn chart_data(trace, options = {}) array => analysis.chart_data(trace, options)
pub fn energy(trace, body, gravity = [0.0, -9.81, 0.0]) array => analysis.energy(trace, body, gravity)
pub fn tracks(trace, body, target = null) array => analysis.tracks(trace, body, target)

pub fn attach(node, options) element^ {
    if (name(node) == 'mesh') <mesh *: node, physics: options, *content(node)>
    else if (name(node) == 'group') <group *: node, physics: options, *content(node)>
    else raise error("physics.attach: requires a mesh or group")
}
