// chart/parse.ls — Parse and validate <chart> element tree
// Extracts structured spec from the chart element tree into a normalized map.

// ============================================================
// Parse a <chart> element into a normalized spec
// ============================================================

pub fn parse_chart(chart_el) {
    let width = if (chart_el.width) chart_el.width else 400;
    let height = if (chart_el.height) chart_el.height else 300;

    // parse padding
    let raw_padding = if (chart_el.padding) chart_el.padding else 20;
    let padding = if (raw_padding is int)
        {top: raw_padding, right: raw_padding, bottom: raw_padding, left: raw_padding}
    else raw_padding;

    // title
    let title = if (chart_el.title) chart_el.title else null;

    // find child elements by tag name
    let children_count = len(chart_el);
    let data_el = find_child(chart_el, 'data', children_count);
    let mark_el = find_child(chart_el, 'mark', children_count);
    let encoding_el = find_child(chart_el, 'encoding', children_count);
    let transform_el = find_child(chart_el, 'transform', children_count);
    let config_el = find_child(chart_el, 'config', children_count);
    let layer_el = find_child(chart_el, 'layer', children_count);
    let facet_el = find_child(chart_el, 'facet', children_count);

    // parse data
    let data = if (data_el) data_el.values else null;

    // parse mark
    let mark = if (mark_el) parse_mark(mark_el) else null;

    // parse encoding channels
    let encoding = if (encoding_el) parse_encoding(encoding_el) else {};

    // parse layer (composition)
    let layer = if (layer_el) parse_layer(layer_el) else null;

    {
        width: width,
        height: height,
        padding: padding,
        title: title,
        data: data,
        mark: mark,
        encoding: encoding,
        transform: transform_el,
        config: config_el,
        layer: layer,
        facet: facet_el
    }
}

// ============================================================
// Find a child element by tag name
// ============================================================

fn find_child(parent_el, tag_name, count) {
    let matches = (for (i in 0 to (count - 1),
                        let child = parent_el[i]
                        where child and name(child) == tag_name) child)
    if (len(matches) > 0) matches[0] else null
}

// ============================================================
// Parse mark element
// ============================================================

fn parse_mark(mark_el) {
    {
        kind: mark_el.kind,
        color: mark_el.color,
        opacity: mark_el.opacity,
        stroke: mark_el.stroke,
        stroke_width: mark_el.stroke_width,
        fill: mark_el.fill,
        interpolate: mark_el.interpolate,
        point: mark_el.point,
        corner_radius: mark_el.corner_radius,
        inner_radius: if (mark_el.inner_radius) mark_el.inner_radius else 0,
        outer_radius: mark_el.outer_radius,
        pad_angle: mark_el.pad_angle,
        size: mark_el.size,
        shape: mark_el.shape,
        font_size: mark_el.font_size,
        stroke_dash: mark_el.stroke_dash
    }
}

// ============================================================
// Parse encoding element
// ============================================================

fn parse_encoding(encoding_el) {
    // look up known channel names directly
    let count = len(encoding_el);
    // build result map from found channels
    let x_el = find_child(encoding_el, 'x', count);
    let y_el = find_child(encoding_el, 'y', count);
    let color_el = find_child(encoding_el, 'color', count);
    let size_el = find_child(encoding_el, 'size', count);
    let opacity_el = find_child(encoding_el, 'opacity', count);
    let theta_el = find_child(encoding_el, 'theta', count);
    let text_el = find_child(encoding_el, 'text', count);
    let stroke_el = find_child(encoding_el, 'stroke', count);
    {
        x: if (x_el) parse_channel(x_el) else null,
        y: if (y_el) parse_channel(y_el) else null,
        color: if (color_el) parse_channel(color_el) else null,
        size: if (size_el) parse_channel(size_el) else null,
        opacity: if (opacity_el) parse_channel(opacity_el) else null,
        theta: if (theta_el) parse_channel(theta_el) else null,
        text: if (text_el) parse_channel(text_el) else null,
        stroke: if (stroke_el) parse_channel(stroke_el) else null
    }
}

fn parse_channel(ch_el) {
    {
        field: ch_el.field,
        dtype: ch_el.dtype,
        value: ch_el.value,
        datum: ch_el.datum,
        title: ch_el.title,
        format: ch_el.format,
        sort: ch_el.sort,
        aggregate: ch_el.aggregate,
        stack: ch_el.stack,
        bin: ch_el.bin,
        zero: ch_el.zero,
        scale: ch_el.scale,
        axis: ch_el.axis,
        legend: ch_el.legend
    }
}

// ============================================================
// Parse layer (composition)
// ============================================================

fn parse_layer(layer_el) {
    let count = len(layer_el)
    (for (i in 0 to (count - 1),
          let child = layer_el[i]
          where name(child) == 'chart') parse_chart(child))
}

// ============================================================
// Get encoding channel by name
// ============================================================

pub fn get_channel(encoding, channel_name: string) {
    encoding[channel_name]
}

// check if encoding has a specific channel
pub fn has_channel(encoding, channel_name: string) bool {
    encoding[channel_name] != null
}
