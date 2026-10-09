import cameras: lambda.map.camera
import models: lambda.map.model
import interactions: lambda.map.interaction
import events: lambda.map.events
import styles: lambda.map.style
import queries: lambda.map.query
import exports: lambda.map.export

pub type Map = <geomap>
pub fn geomap(children, options = {}) => <geomap *:options, *children>
pub fn source(id, data) => <source id:id,type:"geojson",data:data>
pub fn layer(id, kind, source = null, paint = {}, layout = {}) =>
    <layer id:id,type:kind,source:source,paint:paint,layout:layout>
pub fn normalize(model) => models.normalize(model)
pub fn validate(model) => (let result = normalize(model), if (result is error) result else true)
pub fn from_style(style, options = {}) => styles.from_style(style,options)
pub fn project(camera, position) => cameras.project(camera,position)
pub fn unproject(camera, point) => cameras.unproject(camera,point)
pub fn fit_bounds(camera, bounds, padding = 0.0) => cameras.fit_bounds(camera,bounds,padding)
pub fn update(camera, event) => interactions.update(camera,event)
pub fn query_source(model, source_id) => queries.query_source(model,source_id)
pub fn query_rendered(model, point, options = {}) => queries.query_rendered(model,point,options)
pub fn to_svg(model, viewport = null) => exports.to_svg(model,viewport)

pub fn model(spec) => <geomap_view spec:spec>
pub fn interactive(spec) => apply(model(spec))
view <geomap_view> state map_state: null {
    let checked = normalize(~.spec);
    if (checked is error) checked else <geomap
        *:map([for (k,v in checked where k is string or k is symbol) (string(k),v)]),
        *:(if (map_state == null) {} else map_state.camera),
        tabindex:"0", *content(checked)>
}
on pointerdown(evt) {
    map_state = events.dispatch(~.spec,map_state,evt)
    if (map_state.drag != null) { return 'prevent-default' }
    'pass'
}
on pointermove(evt) {
    if (map_state.drag != null) { map_state = events.dispatch(~.spec,map_state,evt) }
}
on pointerup(evt) { map_state = events.dispatch(~.spec,map_state,evt) }
on pointercancel(evt) { map_state = events.dispatch(~.spec,map_state,evt) }
on lostpointercapture(evt) { map_state = events.dispatch(~.spec,map_state,evt) }
on wheel(evt) {
    let before = map_state;
    map_state = events.dispatch(~.spec,map_state,evt)
    if (map_state != before) { return 'prevent-default' }
    'pass'
}
on keydown(evt) {
    if (contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","+","=","-"],evt.key)) {
        let before = map_state;
        map_state = events.dispatch(~.spec,map_state,evt)
        if (map_state != before) { return 'prevent-default' }
    }
    'pass'
}
