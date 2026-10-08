import chart: lambda.chart.chart
import vega: lambda.chart.vega
import svg: lambda.chart.svg

fn elements(tree) => if (tree is element) [tree, for (child in content(tree)) for (item in elements(child)) item] else []
fn tags(tree, tag) => elements(tree) |: name(~) == tag
fn with_class(tree, cls) => elements(tree) |: ~.class == cls
fn resources(tree) => elements(tree) |: name(~) in ['linearGradient', 'radialGradient', 'pattern']
fn references(tree) => [for (el in elements(tree)) for (value in [el.fill, el.stroke]
    where value is string and starts_with(value, "url(#")) slice(value, 5, len(value) - 1)]
fn resolved(tree) {
    let ids = resources(tree) |> ~.id;
    len([for (id in references(tree) where len(ids |: ~ == id) != 1) id]) == 0
}
fn drawable(tree, kind) array | error => content(with_class(tree, "marks " ++ kind)[0])

let linear = {gradient: "linear", stops: [{offset: 0, color: "red"}, {offset: 1, color: "blue", opacity: 0}]}
let radial = {gradient: "radial", x1: 0.3, y1: 0.4, x2: 0.5, y2: 0.6, r1: 0.1, r2: 0.7,
    spread: "reflect", stops: [{offset: 0, color: "white"}, {offset: 1, color: "green"}]}
let hatch = {pattern: "hatch", spacing: 10, angle: 30, color: "black", stroke_width: 2,
    opacity: 0.5, background: "yellow", cross: true}
let rows = [{x: "A", y: 2, group: "first"}, {x: "B", y: 4, group: "second"}]
fn spec(fill, kind = "bar") => <chart width: 240, height: 180,
    <data values: rows> <mark type: kind, fill: fill>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>>
let bars = chart.render(spec(linear))
let points = chart.render(spec(radial, "point"))
let areas = chart.render(spec(hatch, "area"))
let shared = chart.render(<chart <data values: rows> <mark type: "bar", fill: linear, stroke: linear>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>>)
let palette = chart.render(<chart <data values: rows> <mark type: "bar">
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">
        <color field: "group", dtype: "nominal", scale: {domain: ["first", "second", "unused"],
            range: [linear, radial, hatch]}>>>)
let conditional = chart.render(<chart <data values: rows> <mark type: "point">
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">
        <color value: "black", condition: {field: "y", gt: 3, value: radial}>>>)
let identity = chart.render(<chart <data values: [{x: "A", y: 1, color: hatch}]> <mark type: "bar">
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">
        <color field: "color", dtype: "nominal", scale: false>>>)
let calculated = chart.render(<chart <data values: rows>
    <transform <calculate as: "paint", expression: (row) => linear>>
    <mark type: "bar"> <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">
        <color field: "paint", dtype: "nominal", scale: false>>>)
let lines = chart.render(<chart <data values: rows> <mark type: "line", color: linear, point: true>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>>)
let notes = chart.render(<chart <data values: rows> <mark type: "point">
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>
    <annotation <region_note color: hatch, stroke: linear>
        <rule_note y: 1, color: linear> <text_note text: "note", color: radial>>>)
let themed = chart.render(<chart title: "Painted", <data values: rows> <mark type: "bar">
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>
    <config background: linear, title_color: radial, bar: {fill: hatch}>>)
let layers = chart.render(<chart clip: true, <data values: rows>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>
    <layer <chart <mark type: "bar", fill: linear>> <chart <mark type: "point", fill: radial>>
        <chart <mark type: "line", stroke: linear>> >>)
let nested = chart.render(<hconcat spec(linear); <vconcat spec(radial); spec(hatch)>>)
let faceted = chart.render(<chart <data values: rows> <mark type: "bar", fill: linear>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>
    <facet field: "group", columns: 2>>)
let repeated = chart.render(<repeat <column ["y", "z"]>
    <chart <data values: [{x: "A", y: 1, z: 2}]> <mark type: "bar", fill: hatch>
        <encoding <x field: "x", dtype: "nominal"> <y field: {repeat: "column"}, dtype: "quantitative">>>>)
let arcs = chart.render(<chart <data values: rows> <mark type: "arc", fill: radial>
    <encoding <theta field: "y", dtype: "quantitative">>>)
let cloud = chart.render(<chart width: 240, height: 160, <data values: [{text: "Cloud", weight: 1}]>
    <mark type: "wordcloud", color: linear> <encoding <text field: "text"> <size field: "weight">>>)
let converted = chart.render_spec(vega.convert({data: {values: rows}, mark: {type: "bar", fill: linear},
    encoding: {x: {field: "x", type: "nominal"}, y: {field: "y", type: "quantitative"}}}))
let prebuilt = chart.render_spec({concat: "horizontal", spacing: 10, children: [bars, points]})
let defaulted = chart.render(<chart <data values: rows>
    <mark type: "bar", fill: linear, stroke: {*:linear, x1: 0, y1: 0, x2: 1, y2: 0}>
    <encoding <x field: "x", dtype: "nominal"> <y field: "y", dtype: "quantitative">>>)
let ldef = tags(bars, 'linearGradient')[0]
let rdef = tags(points, 'radialGradient')[0]
let pdef = tags(areas, 'pattern')[0]
let checks = [
    {name: "linear gradient defaults", ok: ldef.x1 == 0 and ldef.y1 == 0 and ldef.x2 == 1 and ldef.y2 == 0 and
        ldef.gradientUnits == "objectBoundingBox" and ldef.spreadMethod == "pad"},
    {name: "linear gradient stops retain transparent endpoint", ok: tags(ldef, 'stop')[1]["stop-opacity"] == 0},
    {name: "bar references one reusable paint", ok: len(resources(bars)) == 1 and len(references(bars)) == 2 and resolved(bars)},
    {name: "radial geometry and spread", ok: rdef.cx == 0.5 and rdef.cy == 0.6 and rdef.r == 0.7 and
        rdef.fx == 0.3 and rdef.fy == 0.4 and rdef.fr == 0.1 and rdef.spreadMethod == "reflect"},
    {name: "point radial fill", ok: len(references(points)) == 2 and resolved(points)},
    {name: "hatch dimensions", ok: pdef.patternUnits == "userSpaceOnUse" and pdef.width == 10 and pdef.height == 10 and
        pdef.patternTransform == "rotate(30)"},
    {name: "hatch background and cross stripes", ok: tags(pdef, 'rect')[0].fill == "yellow" and
        contains(tags(pdef, 'path')[0].d, "M0 10 L10 10") and tags(pdef, 'path')[0]["stroke-width"] == 2 and
        tags(pdef, 'path')[0]["stroke-opacity"] == 0.5},
    {name: "area hatch fill", ok: len(references(areas)) == 1 and resolved(areas)},
    {name: "fill and stroke share definition", ok: len(resources(shared)) == 1 and len(references(shared)) == 4 and resolved(shared)},
    {name: "categorical paint palette includes legend-only values", ok: len(resources(palette)) == 3 and
        len(references(palette)) == 5 and resolved(palette)},
    {name: "conditional constant paint", ok: drawable(conditional, "points")[0].fill == "black" and
        starts_with(drawable(conditional, "points")[1].fill, "url(#") and resolved(conditional)},
    {name: "identity color maps", ok: len(references(identity)) == 1 and resolved(identity)},
    {name: "calculated paint field", ok: len(references(calculated)) == 2 and resolved(calculated)},
    {name: "gradient line and point overlay", ok: len(references(lines)) == 3 and resolved(lines)},
    {name: "annotation fills and strokes", ok: len(resources(notes)) == 3 and len(references(notes)) == 4 and resolved(notes)},
    {name: "theme paint cascade", ok: len(resources(themed)) == 3 and len(references(themed)) == 4 and resolved(themed)},
    {name: "layer resources are shared outside clip", ok: len(resources(layers)) == 2 and len(references(layers)) == 5 and
        len(resources(with_class(layers, "plot-clip")[0])) == 0 and resolved(layers)},
    {name: "nested concat resource scope", ok: len(resources(nested)) == 3 and resolved(nested)},
    {name: "facet resource scope", ok: len(resources(faceted)) == 2 and resolved(faceted)},
    {name: "repeat resource scope", ok: len(resources(repeated)) == 2 and resolved(repeated)},
    {name: "arc gradient fill", ok: len(references(arcs)) == 2 and resolved(arcs)},
    {name: "wordcloud gradient color", ok: len(references(cloud)) == 1 and resolved(cloud)},
    {name: "Vega gradient map", ok: len(references(converted)) == 2 and resolved(converted)},
    {name: "prebuilt charts retain distinct paints", ok: len(resources(prebuilt)) == 2 and resolved(prebuilt)},
    {name: "equivalent explicit defaults share a definition", ok: len(resources(defaulted)) == 1 and resolved(defaulted)},
    {name: "deterministic output", ok: chart.render(spec(linear)) == bars},
    {name: "solid fills require no resources", ok: len(resources(chart.render(spec("red")))) == 0},
    {name: "SVG gradient helper preserves zero opacity", ok: tags(svg.linear_gradient("test", 0, 0, 1, 0, linear.stops),
        'stop')[1]["stop-opacity"] == 0}
];
[for (check in checks where check.ok != true) check.name]
