import camera: .camera
import numbers: lambda.chart.numbers

pub fn update(spec, event) {
    let c = camera.camera(spec);
    let kind = string(event.type);
    if (c is error) c
    else if (kind == "pan") camera.pan(c,event.dx,event.dy)
    else if (kind == "zoom") camera.zoom_at(c,event.zoom,event.point)
    else if (kind == "wheel") {
        let delta = if (event.delta_y != null) event.delta_y else event.deltaY;
        if (not numbers.finite_number(delta)) error("map: wheel requires finite delta_y")
        else camera.zoom_at(c,c.zoom-delta/400.0,event.point)
    } else if (kind == "rotate") camera.camera({*:c,bearing:event.bearing})
    else if (kind == "fit_bounds") camera.fit_bounds(c,event.bounds,if (event.padding == null) 0 else event.padding)
    else c
}
