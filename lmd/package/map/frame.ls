import radiant
import models: .model
import cameras: .camera

// frames own evaluated paths and feature values; subsequent model edits cannot change them.
pub fn plan(model, viewport = null) {
    let checked=models.normalize(model);
    let camera=if (checked is error) checked else cameras.camera({*:checked,
        *:(if (viewport==null) {} else {width:viewport.width,height:viewport.height})});
    if (checked is error) checked else if (camera is error) camera else {
        let frame=radiant.geomap_plan(checked,camera.width,camera.height);
        if (frame is string) error("map: " ++ frame)
        else if (frame==null) error("map: frame planning failed") else frame
    }
}
pub fn render_frame(frame) {
    if (frame.type!="geomap-frame" or not (frame.svg is string)) error("map: expected a complete frame")
    else {
        let parsed=parse(frame.svg,'xml') ^ {error("map: invalid frame SVG")};
        if (parsed is error) parsed else content(parsed)[0]
    }
}
pub pn snapshot(node) {
    let frame=radiant.geomap_snapshot(node);
    if (frame==null) error("map: viewport has no displayed frame")
    else if (frame is string) error("map: " ++ frame) else frame
}
pub fn query(frame, point_or_box, options = {}) {
    let result=radiant.geomap_query_frame(frame,point_or_box,options);
    if (result is string) error("map: " ++ result) else result
}
pub pn query_displayed(node, point_or_box, options = {}) {
    let result=radiant.geomap_query_displayed(node,point_or_box,options);
    if (result is string) error("map: " ++ result) else result
}
