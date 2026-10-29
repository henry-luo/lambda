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
