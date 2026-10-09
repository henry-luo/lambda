import radiant
import models: .model
import cameras: .camera

pub fn to_svg(model, viewport = null) {
    let checked = models.normalize(model);
    let camera = if (checked is error) checked else cameras.camera({*:checked,
        *:(if (viewport == null) {} else {width:viewport.width,height:viewport.height})});
    if (checked is error) checked else if (camera is error) camera else {
        let source = radiant.geomap_svg(checked,camera.width,camera.height);
        if (source == null) error("map: native SVG export failed") else {
            let parsed = parse(source,'xml') ^ {error("map: invalid native SVG output")};
            // the native stream contains exactly one root; the XML parser supplies a document wrapper.
            if (parsed is error) parsed else content(parsed)[0]
        }
    }
}
