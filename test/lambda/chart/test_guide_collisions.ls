import chart: lambda.chart.chart
import axis: lambda.chart.axis
import ann: lambda.chart.annotation
import layout: lambda.chart.layout
import scale: lambda.chart.scale
import config: lambda.chart.config
import collision: lambda.chart.collision

fn elements(tree) => if (tree is element) [tree, for (child in content(tree)) for (item in elements(child)) item] else []
fn tags(tree, tag) => elements(tree) |: name(~) == tag
fn with_class(tree, cls) => elements(tree) |: ~.class == cls
fn tick_labels(tree) => [for (tick in with_class(tree, "tick")) for (label in tags(tick, 'text')) label]
fn label_strings(tree) => tick_labels(tree) |> join(content(~), "")

let mapping = scale.linear_scale(0, 1, 0, 100)
let options = {values: [0, 1], title_enabled: false, label_overlap: "hide"}
let guide = {key: "x", mapping: mapping, title: null, config: axis.prepare(mapping, options)}
let geometry = {plot_w: 100.0, plot_h: 100.0, plot_x: 20.0, plot_y: 30.0, total_w: 140.0, guides: []}
let plan = axis.label_plan(mapping, 100, 100, guide.config, null, true)
let blocked = plan.rows[0].bounds
let legend_geometry = {*:geometry, guides: [{x: blocked.left + geometry.plot_x, y: blocked.top + geometry.plot_y,
    width: blocked.right - blocked.left, height: blocked.bottom - blocked.top}]}
let legend_culled = layout.resolve_labels([guide], [], legend_geometry)
let same_axis = layout.resolve_labels([guide, guide], [], geometry)
let separate_axis = layout.resolve_labels([guide, {*:guide, config: {*:guide.config, offset: 50}}], [], geometry)
let annotation = ann.prepare(<annotation <text_note px: 0, py: plan.rows[0].y + 100,
    text: "fixed", anchor: "middle">>, null, null, 100, 100, config.light_theme)
let note_culled = layout.resolve_labels([guide], [annotation], geometry)
let kept = axis.x_axis(mapping, 100, 100, note_culled.axes[0].config, null)
let notes = ann.render_annotations(<annotation
    <text_note px: 20, py: 20, text: "first", label_overlap: "hide">
    <text_note px: 20, py: 20, text: "second", label_overlap: "hide">
    <text_note px: 20, py: 60, text: "third", label_overlap: "hide">
    <rule_note y: 0.5>>, null, scale.linear_scale(0, 1, 100, 0), 100, 100, config.light_theme)

fn annotated(policy, clipped = false, layered = false) => chart.render(<chart width: 300, height: 200, padding: 0, clip: clipped,
    <data values: [{x: 0, y: 0}, {x: 1, y: 1}]>
    <encoding <x field: "x", dtype: "quantitative", scale: {nice: false},
        axis: {values: [0, 1], title: null, label_overlap: policy}>
        <y field: "y", dtype: "quantitative", scale: {nice: false}, axis: null>>
    if (layered) <layer <chart <mark type: "point">>
        <chart <mark type: "point"> <annotation <text_note x: 0, y: 0, dy: 18, anchor: "middle", text: "note">>>>
    else <mark type: "point">
    if (not layered) <annotation <text_note x: 0, y: 0, dy: 18, anchor: "middle", text: "note">>
>)
let single = annotated("hide")
let layered = annotated("hide", false, true)
let clipped = annotated("hide", true)
let retained = annotated(false)
let fixed_late = collision.select([
    {id: "optional", bounds: blocked, optional: true, gap: 0},
    {id: "fixed", bounds: blocked, optional: false, gap: 0}])
let a = {left: 0, right: 10, top: 0, bottom: 10}
let b = {left: 12, right: 22, top: 0, bottom: 10}
let styled = ann.prepare(<annotation <text_note text: "Wide", px: 25, py: 40, dx: 2, dy: -3,
    font_family: "serif", font_size: 40, font_weight: 700, font_style: "italic", letter_spacing: 2>>,
    null, null, 100, 100, config.light_theme)
let smaller = ann.prepare(<annotation <text_note text: "Wide", font_family: "serif", font_size: 11,
    font_weight: 700, font_style: "italic", letter_spacing: 2>>, null, null, 100, 100, config.light_theme)
let right = axis.label_plan(mapping, 100, 100, axis.prepare(mapping,
    {*:options, orient: "right", offset: 25}), null, false)
let top = axis.label_plan(mapping, 100, 100, axis.prepare(mapping,
    {*:options, orient: "top", offset: -25, label_angle: -45}), null, true)
let title_culled = layout.resolve_labels([{*:guide, config: axis.prepare(mapping,
    {*:options, title_enabled: true, title: "XXXXXXXXXXXXXXXXXXXXXX", title_padding: -12})}], [], geometry)
let late_fixed = ann.prepare(<annotation
    <text_note px: 20, py: 20, text: "optional", label_overlap: "hide">
    <text_note px: 20, py: 20, text: "fixed">>, null, null, 100, 100, config.light_theme)
let late_plan = layout.resolve_labels([], [late_fixed], geometry)
let optional_legend_note = ann.prepare(<annotation <text_note px: 0, py: plan.rows[0].y + 100,
    text: "optional", anchor: "middle", label_overlap: "hide">>, null, null, 100, 100, config.light_theme)
let note_legend = layout.resolve_labels([], [optional_legend_note], legend_geometry)
let checks = [
    {name: "legend protects its measured rectangle", ok: legend_culled.axes[0].config._visible_rows == [1]},
    {name: "separate axes share collision space", ok: len(same_axis.axes[0].config._visible_rows) == 2 and
        len(same_axis.axes[1].config._visible_rows) == 0},
    {name: "independent axis offsets", ok: len(separate_axis.axes[1].config._visible_rows) == 2},
    {name: "annotation protects authored space", ok: note_culled.axes[0].config._visible_rows == [1]},
    {name: "culled tick marks retained", ok: len(with_class(kept, "tick")) == 2 and len(tick_labels(kept)) == 1},
    {name: "annotation collisions", ok: (tags(notes, 'text') |> join(content(~), "")) == ["first", "third"]},
    {name: "non-text annotations retained", ok: len(tags(notes, 'line')) == 1},
    {name: "single markup integration", ok: label_strings(single) == ["1"]},
    {name: "layer markup integration", ok: label_strings(layered) == ["1"]},
    {name: "annotation position retained", ok: tags(with_class(single, "annotations")[0], 'text')[0].x == 0 and
        tags(with_class(single, "annotations")[0], 'text')[0].y == tags(single, 'circle')[0].cy + 18},
    {name: "clipped invisible notes do not block", ok: label_strings(clipped) == ["0", "1"]},
    {name: "explicit overlap retained", ok: label_strings(retained) == ["0", "1"]},
    {name: "fixed labels have priority independent of order", ok: (fixed_late |> ~.id) == ["fixed"]},
    {name: "minimum gap", ok: not collision.overlaps(a, b, 2) and collision.overlaps(a, b, 3)},
    {name: "disjoint vertical intervals", ok: not collision.overlaps(a, {left: 0, right: 10, top: 12, bottom: 22}, 2)},
    {name: "annotation measured fonts and offsets", ok: styled.items[0].element["font-family"] == "serif" and
        styled.items[0].element["font-weight"] == 700 and styled.items[0].element.x == 27 and
        styled.items[0].element.y == 37 and styled.items[0].bounds.bottom - styled.items[0].bounds.top >
            smaller.items[0].bounds.bottom - smaller.items[0].bounds.top},
    {name: "right axis placed bounds", ok: min(right.rows |> ~.bounds.left) >= 133},
    {name: "top rotated placed bounds", ok: max(top.rows |> ~.bounds.bottom) <= -33},
    {name: "guide titles protect measured bounds", ok: title_culled.axes[0].config._visible_rows == []},
    {name: "later fixed annotations retain priority", ok: late_plan.annotations[0].visible == [1]},
    {name: "optional notes avoid legends", ok: note_legend.annotations[0].visible == []}
];
[for (check in checks where check.ok != true) check.name]
