// Layout dimensions are explicit inputs to the pure renderer (S12.1.1v2).
import parse: .parse
import scale: .scale
import util: .util

fn positive(value) => util.finite_number(value) and value > 0
pub fn viewport(value) {
    if (value == null) {} else if (not (value is map) or
        (value.width != null and not positive(value.width)) or
        (value.height != null and not positive(value.height))) error("chart: viewport width/height must be finite and positive")
    else value
}
pub fn specified(spec, key) => if (spec["_" ++ key ++ "_specified"] != null) spec["_" ++ key ++ "_specified"]
    else spec[key] != null

fn request(spec, key) {
    let original = spec._sizing_request;
    if (original != null) original[key]
    else if (not specified(spec, key) and spec._viewport[key] != null) "auto"
    else spec[key]
}
fn default_size(key) => if (key == "width") 400.0 else 300.0
pub fn normalize_padding(raw) {
    let value = if (raw == null) 20 else raw;
    let resolved = if (value is number) {top: value, right: value, bottom: value, left: value}
    else {top: if (value.top != null) value.top else 0, right: if (value.right != null) value.right else 0,
        bottom: if (value.bottom != null) value.bottom else 0, left: if (value.left != null) value.left else 0};
    if (not (value is number or value is map) or
        len([for (key, item in resolved where not util.finite_number(item) or item < 0) true]) > 0)
        error("chart: padding must be finite and nonnegative")
    else resolved
}
fn discrete_size(spec, key, value) {
    let channel = spec.encoding[if (key == "width") "x" else "y"];
    let mapping = scale.position_scale(channel, spec.data, 0.0, 1.0, spec.mark.kind, key == "width");
    let step = if (value is map) value.step else if (spec.config.view.step != null) spec.config.view.step else 20;
    if (mapping is error) mapping
    else if (not positive(step)) error("chart: discrete sizing step must be finite and positive")
    else if (mapping.kind != "band" and mapping.kind != "point") {
        if (value is map) error("chart: step sizing requires a discrete position scale") else null
    } else float(max([1, len(mapping.domain)])) * float(step)
}
fn dimension(spec, key, value) {
    if (value == "container") {size: if (spec._viewport[key] != null) spec._viewport[key] else default_size(key), fit: true}
    else if (value == "auto" or value is map) {
        let plot = if (value is map or spec._viewport[key] == null) discrete_size(spec, key, value) else null;
        if (plot is error) plot else {
            size: if (plot != null) plot else if (spec._viewport[key] != null) spec._viewport[key] else default_size(key),
            plot: plot, fit: plot == null and spec._viewport[key] != null}
    } else if (value == null) {size: default_size(key), fit: false}
    else if (positive(value)) {size: float(value), fit: false}
    else error("chart: dimensions must be positive numbers, auto, container, or a discrete step")
}

pub fn resolve_view(spec) {
    let ratio = spec.aspect_ratio;
    let width_request = request(spec, "width");
    let height_request = request(spec, "height");
    let width_auto = width_request == "auto" or not specified(spec, "width");
    let height_auto = height_request == "auto" or not specified(spec, "height");
    let from = if (ratio != null and height_auto) "height" else if (ratio != null and width_auto) "width" else null;
    let w = dimension(spec, "width", width_request);
    let h = dimension(spec, "height", height_request);
    let pad = normalize_padding(spec.padding);
    let failure = util.first_error([w, h, pad]);
    if (failure is error) failure
    else if (ratio != null and not positive(ratio)) error("chart: aspect_ratio must be finite and positive")
    else if (from != null and width_request is map and height_request is map) error("chart: aspect_ratio cannot constrain two discrete steps")
    else {
        let width = if (w.plot != null) w.size + pad.left + pad.right else w.size;
        let height = if (h.plot != null) h.size + pad.top + pad.bottom else h.size;
        let final_width = if (from == "width") height * ratio else width;
        let final_height = if (from == "height") width / ratio else height;
        if (not positive(final_width) or not positive(final_height)) error("chart: resolved dimensions are not finite and positive")
        else {*:spec, padding: pad, width: final_width, height: final_height,
            _sizing_request: {width: width_request, height: height_request},
            _plot_width: if (from == "width") null else w.plot,
            _plot_height: if (from == "height") null else h.plot,
            _aspect_from: from, _fit_container: w.fit or h.fit}
    }
}

// Composition budgets constrain flexible children; explicit numeric/step sizes retain their own extent.
fn budget(spec, key) {
    let value = request(spec, key);
    if (value == "container") (if (spec._viewport[key] != null) spec._viewport[key] else default_size(key))
    else if (value == "auto" or not specified(spec, key)) spec._viewport[key]
    else if (positive(value)) value
    else if (value is map) null else if (value == null) null
    else error("chart: invalid composition dimension")
}
pub fn composition(spec, children) {
    let w = budget(spec, "width");
    let h = budget(spec, "height");
    let ratio = spec.aspect_ratio;
    let failure = util.first_error([w, h]);
    if (failure is error) failure else if (ratio != null and not positive(ratio)) error("chart: invalid composition aspect_ratio")
    else {
        let width = if (w == null and h != null and ratio != null) h * ratio else w;
        let height = if (h == null and w != null and ratio != null) w / ratio else h;
        let key = if (spec.concat == "horizontal") "width" else "height";
        if ((width != null and not positive(width)) or (height != null and not positive(height)))
            error("chart: resolved composition dimensions are not finite and positive")
        else {width: width, height: height, key: key,
            flexible: [for (child in children) not (child is element and name(child) == 'svg') and
                (not specified(child, key) or child[key] == "auto" or child[key] == "container")],
            viewport: spec._viewport}
    }
}

pub fn inherit_requests(parent, child) {
    {*:child, *:map([for (key in ["width", "height"] where parent[key] is map and not specified(child, key))
        for (item in [key, parent[key], "_" ++ key ++ "_specified", true]) item])}
}
pub fn allocate(plan, fixed, spacing) {
    let dimension = plan[plan.key];
    let flexible_count = len(plan.flexible |: ~ == true);
    let available = if (dimension != null) float(dimension) -
        max([0, len(fixed) - 1]) * spacing - sum([for (image in fixed where image != null)
            if (image[plan.key] != null) float(image[plan.key]) else default_size(plan.key)]) else null;
    if (dimension != null and flexible_count > 0 and available <= 0) error("chart: composition has no space for flexible children")
    else [for (index, image in fixed) {
        *:parse.attributes(plan.viewport), width: plan.width, height: plan.height,
        *:if (dimension != null and plan.flexible[index]) map([plan.key, available / float(flexible_count)]) else {}
    }]
}

pub fn repeat_viewport(spec, rows, columns, spacing) {
    let plan = composition({*:spec, concat: "horizontal"}, []);
    if (plan is error) plan else if (rows <= 0 or columns <= 0) error("chart: repeat requires nonempty rows and columns") else {
        let width = if (plan.width != null) (plan.width - max([0, columns - 1]) * spacing) / float(columns) else null;
        let height = if (plan.height != null) (plan.height - max([0, rows - 1]) * spacing) / float(rows) else null;
        viewport({width: width, height: height})
    }
}
