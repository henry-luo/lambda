import chart: lambda.chart.chart
import vega: lambda.chart.vega
import svg: lambda.chart.svg

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn tags(node, tag) => elements(node) |: name(~) == tag
fn classes(node, class_name) => elements(node) |: ~.class == class_name
let base = {width: 200, height: 100, padding: 0, encoding: {
    x: {field: "x", dtype: "quantitative", axis: null, scale: {domain: [0, 10], nice: false}},
    y: {field: "y", dtype: "quantitative", axis: null, scale: {domain: [0, 10], nice: false}}}};
let trail = chart.render_spec({*:base, data: [{x: 0, y: 5, width: 2}, {x: 10, y: 5, width: 10}], mark: {kind: "trail", opacity: 0.5},
    encoding: {*:base.encoding, size: {field: "width", dtype: "quantitative", scale: null, legend: null}, tooltip: {field: "width"}}});
let default_trail = chart.render_spec({*:base, data: [{x: 0, y: 5, width: 2}, {x: 10, y: 5, width: 10}], mark: {kind: "trail"},
    encoding: {*:base.encoding, size: {field: "width", dtype: "quantitative", legend: null}}});
let width_legend = chart.render_spec({*:base, width: 400, data: [{x: 0, y: 5, width: 2}, {x: 10, y: 5, width: 10}],
    mark: {kind: "trail"}, encoding: {*:base.encoding, size: {field: "width", dtype: "quantitative",
        scale: {domain: [2, 10], range: [2, 10]}, legend: {values: [2, 10]}}}});
let grouped_trail = chart.render_spec({*:base, mark: {kind: "trail"}, data: [
    {x: 0, y: 0, group: "a"}, {x: 10, y: 10, group: "a"}, {x: 0, y: 10, group: "b"}, {x: 10, y: 0, group: "b"}],
    encoding: {*:base.encoding, detail: {field: "group"}}});
let coincident = chart.render_spec({*:base, mark: {kind: "trail", size: 4}, data: [{x: 5, y: 5}, {x: 5, y: 5}]});
let slope = chart.render_spec({*:base, mark: {kind: "slope"}, data: [
    {x: 10, y: 5, entity: "a"}, {x: 0, y: 1, entity: "a"}, {x: 0, y: 8, entity: "b"}, {x: 10, y: 3, entity: "b"}],
    encoding: {*:base.encoding, detail: {field: "entity"}, y: {*:base.encoding.y, scale: {*:base.encoding.y.scale, reverse: true}}}});
let source = "data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/svg'/%3E";
let images = chart.render(<chart width: 200, height: 100, padding: 0,
    <data values: [{x: 5, y: 5, url: source}]> <mark type: "image", width: 40, height: 20>
    <encoding <x field: "x", dtype: "quantitative", scale: {domain: [0, 10]}, axis: null>
        <y field: "y", dtype: "quantitative", scale: {domain: [0, 10]}, axis: null>
        <url field: "url"> <opacity value: 0> <tooltip field: "url">>>);
let ranged_image = chart.render_spec({*:base, data: [{x: 8, y: 2, end_x: 2, end_y: 8}], mark: {kind: "image", url: source},
    encoding: {*:base.encoding, x2: {field: "end_x"}, y2: {field: "end_y"}}});
let distribution = [{category: "a", value: 0, density: 0}, {category: "a", value: 5, density: 1},
    {category: "a", value: 10, density: 0}, {category: "b", value: 0, density: 0},
    {category: "b", value: 5, density: 0.5}, {category: "b", value: 10, density: 0}];
let violin_spec = {*:base, data: distribution, mark: {kind: "violin", density_field: "density", width: 40}, encoding: {
    x: {field: "category", dtype: "nominal", axis: null}, y: {*:base.encoding.y, field: "value"}}};
let violin = chart.render_spec(violin_spec);
let shared_violin = chart.render_spec({*:violin_spec, mark: {*:violin_spec.mark, density_resolve: "shared"}});
let automatic_violin = chart.render_spec({*:violin_spec, mark: {kind: "violin", steps: 8}});
let horizontal_violin = chart.render_spec({*:violin_spec, encoding: {
    y: violin_spec.encoding.x, x: violin_spec.encoding.y}});
let vg = chart.render_spec(vega.convert({data: {values: [{x: 1, y: 1, url: source}]}, mark: {type: "image", width: 20, height: 10},
    encoding: {x: {field: "x", type: "quantitative"}, y: {field: "y", type: "quantitative"}, url: {field: "url"}}}));
let full = svg.arc_path(0, 0, 0, 10, 0, 6.283185307179586);
let donut = svg.arc_path(0, 0, 5, 10, 0, 6.283185307179586);
let checks = {
    tapered_geometry: starts_with(tags(trail, 'path')[0].d, "M0 49 L200 45 L200 55 L0 51"),
    round_joins: len(split(tags(trail, 'path')[0].d, "A")) == 5,
    translucent_path: len(tags(trail, 'path')) == 1 and tags(trail, 'path')[0].opacity == 0.5,
    trail_tooltip: tags(trail, 'title')[0][0] == "2",
    default_width_scale: contains(tags(default_trail, 'path')[0].d, "L200 45"),
    width_legend: tags(classes(width_legend, "legend size-legend")[0], 'line')[0]["stroke-width"] == 2 and
        tags(classes(width_legend, "legend size-legend")[0], 'line')[1]["stroke-width"] == 10,
    grouped_trails: len(tags(grouped_trail, 'path')) == 2,
    coincident_points: len(tags(coincident, 'path')) == 1 and not contains(tags(coincident, 'path')[0].d, "nan"),
    slope_groups: len(classes(slope, "marks slopes")) == 1 and len(tags(slope, 'path')) == 2,
    slope_order: starts_with(tags(slope, 'path')[0].d, "M0 10 L200 50"),
    image_markup: tags(images, 'image')[0].href == source and tags(images, 'image')[0].x == 80 and tags(images, 'image')[0].y == 40,
    image_style: tags(images, 'image')[0].width == 40 and tags(images, 'image')[0].opacity == 0,
    image_tooltip: tags(images, 'title')[0][0] == source,
    ranged_image: tags(ranged_image, 'image')[0].x == 40 and tags(ranged_image, 'image')[0].y == 20 and
        tags(ranged_image, 'image')[0].width == 120 and tags(ranged_image, 'image')[0].height == 60,
    violin_count: len(classes(violin, "marks violins")) == 1 and len(tags(violin, 'path')) == 2,
    mirrored_density: contains(tags(violin, 'path')[0].d, "L31 50") and contains(tags(violin, 'path')[0].d, "L71 50"),
    shared_density: contains(tags(shared_violin, 'path')[1].d, "L139 50") and contains(tags(shared_violin, 'path')[1].d, "L159 50"),
    automatic_density: len(tags(automatic_violin, 'path')) == 2 and len(split(tags(automatic_violin, 'path')[0].d, "L")) > 10,
    horizontal_density: horizontal_violin is element and len(tags(horizontal_violin, 'path')) == 2,
    vega_image: len(tags(vg, 'image')) == 1 and tags(vg, 'image')[0].height == 10,
    full_circle: len(split(full, "A")) == 3,
    full_donut: len(split(donut, "A")) == 5,
    empty_trail: chart.render_spec({*:base, data: [], mark: {kind: "trail"}}) is element,
    empty_slope: chart.render_spec({*:base, data: [], mark: {kind: "slope"}}) is element,
    bad_trail_width: chart.render_spec({*:base, data: [{x: 1, y: 1}], mark: {kind: "trail", size: -1}}) is error,
    bad_trail_curve: chart.render_spec({*:base, data: [{x: 1, y: 1}], mark: {kind: "trail", interpolate: "cardinal"}}) is error,
    bad_image_url: chart.render_spec({*:base, data: [{x: 1, y: 1}], mark: {kind: "image"}}) is error,
    bad_image_size: chart.render_spec({*:base, data: [{x: 1, y: 1}], mark: {kind: "image", url: source, width: -1}}) is error,
    bad_slope_pair: chart.render_spec({*:base, data: [{x: 1, y: 1}], mark: {kind: "slope"}}) is error,
    bad_violin_density: chart.render_spec({*:violin_spec, data: [{category: "a", value: 5, density: -1}]}) is error
};
[for (label, passed in checks where passed != true) string(label)]
