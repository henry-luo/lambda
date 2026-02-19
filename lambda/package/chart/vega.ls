// chart/vega.ls — Convert Vega-Lite JSON spec to Lambda chart spec map
// Takes a parsed JSON (map) representing a Vega-Lite specification
// and returns a normalized spec map compatible with chart.render_spec().

// ============================================================
// Public API: convert Vega-Lite JSON to chart spec
// ============================================================

pub fn convert(vl) {
    let width = if (vl.width) vl.width else 400;
    let height = if (vl.height) vl.height else 300;

    // parse padding
    let raw_padding = if (vl.padding) vl.padding else 20;
    let padding = if (raw_padding is int)
        {top: raw_padding, right: raw_padding, bottom: raw_padding, left: raw_padding}
    else raw_padding;

    // title: can be string or {text: ...}
    let title = if (vl.title is string) vl.title
        else if (vl.title and vl.title.text) vl.title.text
        else null;

    // data
    let data = if (vl.data and vl.data.values) vl.data.values else null;

    // mark
    let mark = convert_mark(vl.mark);

    // encoding
    let encoding = if (vl.encoding) convert_encoding(vl.encoding) else {};

    // transforms
    let transform = if (vl.transform) convert_transforms(vl.transform) else null;

    // layer
    let layer = if (vl.layer) convert_layer(vl.layer, data) else null;

    {
        width: width,
        height: height,
        padding: padding,
        title: title,
        data: data,
        mark: mark,
        encoding: encoding,
        transform: transform,
        config: null,
        layer: layer,
        facet: null
    }
}

// ============================================================
// Mark conversion
// ============================================================

fn convert_mark(mark_json) {
    (if (mark_json is string)
        { kind: mark_json, color: null, opacity: null, stroke: null,
          stroke_width: null, fill: null, interpolate: null, point: null,
          corner_radius: null, inner_radius: 0, outer_radius: null,
          pad_angle: null, size: null, shape: null, font_size: null,
          stroke_dash: null }
    else if (mark_json)
        (let kind = (if (mark_json["type"]) mark_json["type"]
            else if (mark_json.kind) mark_json.kind
            else "point"),
        {
            kind: kind,
            color: mark_json.color,
            opacity: mark_json.opacity,
            stroke: mark_json.stroke,
            stroke_width: vl_key(mark_json, "strokeWidth", "stroke_width"),
            fill: mark_json.fill,
            interpolate: mark_json.interpolate,
            point: mark_json.point,
            corner_radius: vl_key(mark_json, "cornerRadius", "corner_radius"),
            inner_radius: (if (vl_key(mark_json, "innerRadius", "inner_radius"))
                vl_key(mark_json, "innerRadius", "inner_radius") else 0),
            outer_radius: vl_key(mark_json, "outerRadius", "outer_radius"),
            pad_angle: vl_key(mark_json, "padAngle", "pad_angle"),
            size: mark_json.size,
            shape: mark_json.shape,
            font_size: vl_key(mark_json, "fontSize", "font_size"),
            stroke_dash: vl_key(mark_json, "strokeDash", "stroke_dash")
        })
    else null)
}

// lookup a key with camelCase or snake_case fallback
fn vl_key(m, camel: string, snake: string) {
    (if (m[camel]) m[camel] else m[snake])
}

// ============================================================
// Encoding conversion
// ============================================================

fn convert_encoding(enc) {
    {
        x: if (enc.x) convert_channel(enc.x) else null,
        y: if (enc.y) convert_channel(enc.y) else null,
        color: if (enc.color) convert_channel(enc.color) else null,
        size: if (enc.size) convert_channel(enc.size) else null,
        opacity: if (enc.opacity) convert_channel(enc.opacity) else null,
        theta: if (enc.theta) convert_channel(enc.theta) else null,
        text: if (enc.text) convert_channel(enc.text) else null,
        stroke: if (enc.stroke) convert_channel(enc.stroke) else null
    }
}

fn convert_channel(ch) {
    {
        field: ch.field,
        dtype: vl_type(ch["type"]),
        value: ch.value,
        datum: ch.datum,
        title: ch.title,
        format: ch.format,
        sort: ch.sort,
        aggregate: ch.aggregate,
        stack: ch.stack,
        bin: ch.bin,
        zero: ch.zero,
        scale: ch.scale,
        axis: ch.axis,
        legend: ch.legend
    }
}

// map Vega-Lite type names to Lambda chart dtype names
fn vl_type(t) {
    if (t == "quantitative") "quantitative"
    else if (t == "nominal") "nominal"
    else if (t == "ordinal") "ordinal"
    else if (t == "temporal") "temporal"
    else if (t == "Q") "quantitative"
    else if (t == "N") "nominal"
    else if (t == "O") "ordinal"
    else if (t == "T") "temporal"
    else t
}

// ============================================================
// Transform conversion
// ============================================================

fn convert_transforms(transforms) {
    // transforms is an array of transform objects
    // we return them as-is since chart.transform module handles maps
    transforms
}

// ============================================================
// Layer conversion
// ============================================================

fn convert_layer(layers, parent_data) {
    (for (layer in layers)
        (let layer_data = if (layer.data and layer.data.values) layer.data.values
            else parent_data,
        {
            width: if (layer.width) layer.width else 400,
            height: if (layer.height) layer.height else 300,
            padding: {top: 20, right: 20, bottom: 20, left: 20},
            title: null,
            data: layer_data,
            mark: convert_mark(layer.mark),
            encoding: if (layer.encoding) convert_encoding(layer.encoding) else {},
            transform: null,
            config: null,
            layer: null,
            facet: null
        }))
}
