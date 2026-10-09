import cameras: lambda.map.camera
import models: lambda.map.model
import interactions: lambda.map.interaction
import events: lambda.map.events
import styles: lambda.map.style
import queries: lambda.map.query
import exports: lambda.map.export
import frames: lambda.map.frame
import controls: lambda.map.controls

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
pub fn plan(model, viewport = null) => frames.plan(model,viewport)
pub fn render_frame(frame) => frames.render_frame(frame)
pub pn snapshot(node) { frames.snapshot(node) }
pub pn query_displayed(node, point_or_box, options = {}) { frames.query_displayed(node,point_or_box,options) }
pub fn to_svg(model, viewport = null) => exports.to_svg(model,viewport)

pub fn model(spec, options = {}) => <geomap_view spec:spec, options:options>
pub fn interactive(spec, options = {}) => apply(model(spec,options))
view <geomap_view> state map_state: null {
    let checked = normalize(~.spec);
    let image=if (checked is error) checked else <geomap
        *:map([for (k,v in checked where k is string or k is symbol) (string(k),v)]),
        *:(if (map_state == null) {} else map_state.camera),
        tabindex:"0",role:"region",'aria-label':if (~.options.label==null) "Interactive map" else ~.options.label,
        'data-map-hover':format(if (map_state==null) [] else map_state.hover |> ~.feature_id,'json'),
        'data-map-selection':format(if (map_state==null) [] else map_state.selection |> ~.feature_id,'json'),
        *content(checked)>;
    if (image is error) image
    else if (~.options.controls==true or ~.options.feature_list==true) <div 'data-map-host':"",image;
        if (~.options.controls==true) controls.controls(~.options);
        if (~.options.feature_list==true) controls.features(if (map_state==null) [] else map_state.selection)
    > else image
}
on pointerdown(evt) {
    map_state = events.dispatch(~.spec,map_state,evt,~.options)
    if (map_state.drag != null) { return 'prevent-default' }
    'pass'
}
on pointermove(evt) { map_state = events.dispatch(~.spec,map_state,evt,~.options) }
on pointerleave(evt) { map_state = events.dispatch(~.spec,map_state,evt,~.options) }
on click(evt) { map_state = events.dispatch(~.spec,map_state,evt,~.options) }
on dblclick(evt) {
    map_state = events.dispatch(~.spec,map_state,evt,~.options)
    'prevent-default'
}
on pointerup(evt) { map_state = events.dispatch(~.spec,map_state,evt,~.options) }
on pointercancel(evt) { map_state = events.dispatch(~.spec,map_state,evt,~.options) }
on lostpointercapture(evt) { map_state = events.dispatch(~.spec,map_state,evt,~.options) }
on wheel(evt) {
    let before = map_state;
    map_state = events.dispatch(~.spec,map_state,evt,~.options)
    if (map_state != before) { return 'prevent-default' }
    'pass'
}
on keydown(evt) {
    if (contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","+","=","-","Home","Escape"],evt.key)) {
        let before = map_state;
        map_state = events.dispatch(~.spec,map_state,evt,~.options)
        if (map_state != before) { return 'prevent-default' }
    }
    'pass'
}
