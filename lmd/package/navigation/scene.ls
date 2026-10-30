// S12.1.1v3/D7.5.3v2: cached navigation belongs to the live document owner.
import radiant

pub fn bake_asset(id,sources,settings,options = {}) element => <'navigation-bake' id:id,version:1,*:options,sources:sources,settings:settings>
pub pn bake_start(node,asset) { return radiant.scene3d(node,{operation:"navigation_bake_start",asset:asset}) }
pub pn bake_poll(node,token) { return radiant.scene3d(node,{operation:"navigation_bake_poll",token:token}) }
pub pn bake_publish(node,token) { return radiant.scene3d(node,{operation:"navigation_bake_publish",token:token}) }
pub pn bake_cancel(node,token) { return radiant.scene3d(node,{operation:"navigation_bake_cancel",token:token}) }
pub pn bake_release(node,token) { return radiant.scene3d(node,{operation:"navigation_bake_release",token:token}) }
pub pn bake_export(node,token) { return radiant.scene3d(node,{operation:"navigation_bake_export",token:token}) }

pub pn path(node, asset, start, goal, options = {}, generation = null) {
    return radiant.scene3d(node, {operation: "navigation_path", asset: asset,
        start: start, goal: goal, options: options, generation: generation})
}
pub pn snapshot(node) { return radiant.scene3d(node, {operation: "navigation_read"}) }
pub pn current(node, asset, generation) {
    return radiant.scene3d(node, {operation: "navigation_current", asset: asset, generation: generation})
}

// agent generations guard commands; path generations separately guard link acknowledgements.
pn agent_command(node, id, operation, fields = {}, generation = null) {
    return radiant.scene3d(node, {operation: operation, agent: id, generation: generation, *: fields})
}
pub pn agent_attach(node, id, asset, movement, options = {}) {
    return agent_command(node,id,"navigation_agent_attach",{asset:asset,movement:movement,options:options})
}
pub pn agent_target(node, id, target, generation = null) {
    return agent_command(node,id,"navigation_agent_target",{target:target},generation)
}
pub pn agent_snapshot(node, id) { return agent_command(node,id,"navigation_agent_read") }
pub pn agent_avoidance(node, id, options = {}, generation = null) {
    return agent_command(node,id,"navigation_agent_avoidance",{options:options},generation)
}
pub pn agent_cancel(node, id, generation = null) { return agent_command(node,id,"navigation_agent_cancel",{},generation) }
pub pn agent_release(node, id, generation = null) { return agent_command(node,id,"navigation_agent_release",{},generation) }
pub pn agent_link_complete(node, id, path_generation, action, generation = null) {
    return agent_command(node,id,"navigation_agent_link_complete",{path_generation:path_generation,action:action},generation)
}
