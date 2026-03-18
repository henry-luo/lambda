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
    let annotation_el = find_child(chart_el, 'annotation', children_count);

    // parse data: support both {values: [...]} and inline <row> children
    let data = if (data_el and data_el.values) data_el.values
        else if (data_el and len(data_el) > 0)
            (for (i in 0 to (len(data_el) - 1),
                  let child = data_el[i]
                  where child != null) child)
        else null;

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
        facet: facet_el,
        annotation: annotation_el
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
    let mk = if (mark_el.kind) mark_el.kind else mark_el['type'];
    {
        kind: mk,
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
        stroke_dash: mark_el.stroke_dash,
        width: mark_el.width
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
    let x_offset_el = find_child(encoding_el, 'x_offset', count);
    let x2_el = find_child(encoding_el, 'x2', count);
    let y2_el = find_child(encoding_el, 'y2', count);
    let detail_el = find_child(encoding_el, 'detail', count);
    let tooltip_el = find_child(encoding_el, 'tooltip', count);
    {
        x: if (x_el) parse_channel(x_el) else null,
        y: if (y_el) parse_channel(y_el) else null,
        color: if (color_el) parse_channel(color_el) else null,
        size: if (size_el) parse_channel(size_el) else null,
        opacity: if (opacity_el) parse_channel(opacity_el) else null,
        theta: if (theta_el) parse_channel(theta_el) else null,
        text: if (text_el) parse_channel(text_el) else null,
        stroke: if (stroke_el) parse_channel(stroke_el) else null,
        x_offset: if (x_offset_el) parse_channel(x_offset_el) else null,
        x2: if (x2_el) parse_channel(x2_el) else null,
        y2: if (y2_el) parse_channel(y2_el) else null,
        detail: if (detail_el) parse_channel(detail_el) else null,
        tooltip: if (tooltip_el) parse_channel(tooltip_el) else null
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
        legend: ch_el.legend,
        condition: ch_el.condition
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

// ============================================================
// Parse concat composition (<hconcat> or <vconcat>)
// ============================================================

pub fn parse_concat(concat_el) {
    let direction = if (name(concat_el) == 'hconcat') "horizontal" else "vertical";
    let spacing = if (concat_el.spacing) concat_el.spacing else 20;
    let count = len(concat_el);
    let children = (for (i in 0 to (count - 1),
                         let child = concat_el[i]
                         where child != null) parse_top(child));
    {
        concat: direction,
        spacing: spacing,
        children: children
    }
}

// ============================================================
// Parse repeat composition (<repeat>)
// ============================================================

pub fn parse_repeat(repeat_el) {
    let count = len(repeat_el);
    let children = for (i in 0 to (count - 1),
                        let child = repeat_el[i]
                        where child != null) child;
    let row_el = (for (c in children where name(c) == 'row') c);
    let col_el = (for (c in children where name(c) == 'column') c);
    let row_fields = if (len(row_el) > 0) row_el[0][0] else null;
    let col_fields = if (len(col_el) > 0) col_el[0][0] else null;
    let template = (for (c in children where name(c) == 'chart') c);
    let tmpl = if (len(template) > 0) template[0] else null;
    {
        repeat_row: row_fields,
        repeat_column: col_fields,
        template: tmpl
    }
}

// ============================================================
// Top-level parse: detects chart, hconcat, vconcat, or repeat
// ============================================================

pub fn parse_top(el) {
    let tag = name(el);
    if (tag == 'hconcat' or tag == 'vconcat') parse_concat(el)
    else if (tag == 'repeat') parse_repeat(el)
    else parse_chart(el)
}
