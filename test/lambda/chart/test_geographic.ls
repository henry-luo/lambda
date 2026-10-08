import chart: lambda.chart.chart
import vega: lambda.chart.vega
import projection: lambda.chart.projection
import geometry: lambda.chart.geometry
import geo: lambda.chart.geo
import util: lambda.chart.util

fn elements(node) => if (node is element) [node, for (child in content(node)) for (item in elements(child)) item] else []
fn classes(node, label) => elements(node) |: ~.class == label
fn near(a, b) => abs(a - b) < 0.000001
let square = {type: "Polygon", coordinates: [[[-30, -20], [30, -20], [30, 20], [-30, 20], [-30, -20]]]};
let hole = [[-10, -10], [-10, 10], [10, 10], [10, -10], [-10, -10]];
let holed = {*:square, coordinates: [*square.coordinates, hole]};
let features = {type: "FeatureCollection", features: [
    {type: "Feature", id: "west", properties: {score: 1, region: "West"}, geometry: square},
    {type: "Feature", id: "east", properties: {score: 9, region: "East"},
        geometry: {type: "Polygon", coordinates: [[[60, -20], [100, -20], [100, 20], [60, 20], [60, -20]]]}}]};
let base = {width: 360, height: 180, padding: 0, mark: {kind: "geoshape", projection: "equirectangular"}, data: features,
    encoding: {color: {field: "score", dtype: "quantitative", legend: null}, tooltip: {field: "region"}}};
let eq = projection.configure(360, 180, "equirectangular");
let merc = projection.configure(200, 200, "mercator");
let ortho = projection.configure(200, 200, "orthographic");
let ne = projection.configure(200, 200, "natural_earth");
let albers = projection.configure(200, 200, {type: "albers", scale: 100, translate: [0, 0]});
let image = chart.render_spec(base);
let polygons = classes(image, "geo-shape");
let hole_svg = chart.render_spec({*:base, data: holed, encoding: {}, mark: {*:base.mark, stroke: "black"}});
let shapes = [for (kind in ["mercator", "equirectangular", "albers", "orthographic", "natural_earth"])
    chart.render_spec({*:base, mark: {*:base.mark, projection: {type: kind}}, encoding: {}})];
let points = chart.render_spec({*:base, data: [{lon: 0, lat: 0, size: 25}, {lon: 180, lat: 0, size: 25}],
    mark: {kind: "geoshape", projection: "orthographic"}, encoding: {
        longitude: {field: "lon"}, latitude: {field: "lat"}, size: {field: "size", scale: null}}});
let lines = chart.render_spec({*:base, data: {type: "GeometryCollection", geometries: [
    {type: "MultiPoint", coordinates: [[0, 0], [20, 20]]},
    {type: "MultiLineString", coordinates: [[[-160, 0], [160, 0]], [[0, -90], [0, 90]]]},
    {type: "MultiPolygon", coordinates: [square.coordinates, holed.coordinates]}]}, encoding: {}});
let crossing = chart.render_spec({*:base, data: {type: "LineString", coordinates: [[160, 0], [180, 0]]},
    mark: {kind: "geoshape", projection: {type: "equirectangular", center: [-170, 0]}}});
let limb = chart.render_spec({*:base, width: 200, height: 200, data: {type: "Polygon", coordinates: [[[70, -20], [110, -20], [110, 20], [70, 20], [70, -20]]]},
    encoding: {}, mark: {kind: "geoshape", projection: "orthographic"}});
let back = chart.render_spec({*:base, data: {type: "Polygon", coordinates: [[[120, -20], [150, -20], [150, 20], [120, 20], [120, -20]]]},
    encoding: {}, mark: {kind: "geoshape", projection: "orthographic"}});
let concave = [[-30, -20], [30, -20], [30, 20], [0, 0], [-30, 20], [-30, -20]];
let triangles = geometry.triangulate(concave);
let mesh = [for (triangle in geometry.triangulate(square.coordinates[0])) for (piece in projection.triangle(triangle, ortho)) piece];
let checks = {
    eq_center: projection.project([0, 0], eq) == [180.0, 90.0],
    eq_corner: projection.project([-180, 90], eq) == [0.0, 0.0],
    mercator_square: near(merc.scale, 100.0 / util.PI),
    mercator_poles: projection.project([0, 90], merc) == null,
    mercator_limit: near(projection.project([0, merc.latitude_limit], merc)[1], 0.0),
    ortho_center: projection.project([0, 0], ortho) == [100.0, 100.0],
    ortho_limb: near(projection.project([90, 0], ortho)[0], 200.0),
    ortho_back: projection.project([120, 0], ortho) == null,
    tilted_center: projection.project([10, 40], projection.configure(200, 200, {type: "orthographic", center: [10, 40]})) == [100.0, 100.0],
    natural_formula: near(projection.raw([180, 0], ne)[0], util.PI * 0.8707),
    albers_equator: near(projection.raw([0, 0], albers)[0], 0.0) and near(projection.raw([0, 0], albers)[1], 0.0),
    albers_symmetric_parallels: projection.configure(200, 200, {type: "albers", parallels: [-30, 30]}).n == 0,
    properties_and_color: len(polygons) == 2 and polygons[0].fill != polygons[1].fill,
    tooltip: (elements(image) |: name(~) == 'title')[0][0] == "West",
    polygon_hole: len(classes(hole_svg, "geo-shape")) == 1 and classes(hole_svg, "geo-shape")[0]['fill-rule'] == "nonzero" and
        len(split(classes(hole_svg, "geo-shape")[0].d, " Z")) == 3,
    boundary_stroke: len(classes(hole_svg, "geo-outline")) == 1 and classes(hole_svg, "geo-shape")[0].stroke == "none",
    five_projections: len([for (image in shapes where image is element and len(classes(image, "geo-shape")) > 0) true]) == 5,
    visible_points: len(classes(points, "geo-point")) == 1 and near(classes(points, "geo-point")[0].r, math.sqrt(25.0 / util.PI)),
    collections: len(classes(lines, "geo-point")) == 2 and len(classes(lines, "geo-line")) == 2 and len(classes(lines, "geo-shape")) == 2,
    longitude_wrap: near(projection.project([170, 0], projection.configure(360, 180, {type: "equirectangular", center: [-170, 0]}))[0], 160.0),
    seam_line: contains(classes(crossing, "geo-line")[0].d, "M150 90") and not contains(classes(crossing, "geo-line")[0].d, "L350"),
    clipped_limb: len(classes(limb, "geo-shape")) == 1 and not contains(classes(limb, "geo-shape")[0].d, "nan"),
    backface_omitted: len(classes(back, "geo-shape")) == 0,
    concave_triangulation: len(triangles) == 3 and near(sum(triangles |> geometry.area(~)), geometry.area(slice(concave, 0, 5))),
    clipped_mesh: len(mesh) > 2 and len([for (piece in mesh) for (point in piece where (point[0] - 100.0) ** 2 + (point[1] - 100.0) ** 2 > 10000.000001) true]) == 0,
    projected_area: abs(sum(mesh |> abs(geometry.area(~))) -
        2.0 * math.sin(util.deg_to_rad(30.0)) * (util.deg_to_rad(20.0) + math.sin(util.deg_to_rad(40.0)) / 2.0) * 10000.0) < 100.0,
    vega_geojson: len(classes(chart.render_spec(vega.convert({width: 360, height: 180, padding: 0,
        data: {values: features.features}, mark: "geoshape", projection: {type: "equirectangular"}, encoding: {color: {field: "score", type: "Q", legend: null}}})), "geo-shape")) == 2,
    native_markup: len(classes(chart.render(<chart width: 360, height: 180, padding: 0, projection: {type: "equirectangular"},
        <data values: features> <mark type: "geoshape">>), "geo-shape")) == 2,
    empty: chart.render_spec({*:base, data: []}) is element,
    null_geometry: len(classes(chart.render_spec({*:base, data: [{type: "Feature", geometry: null}], encoding: {}}), "geo-shape")) == 0,
    shape_encoding: len(classes(chart.render_spec({*:base, data: [{outline: square}],
        encoding: {shape: {field: "outline", dtype: "geojson"}}}), "geo-shape")) == 1,
    inherited_geojson: len(classes(chart.render_spec({width: 360, height: 180, padding: 0, data: features,
        layer: [{mark: {kind: "geoshape", projection: "equirectangular"}}]}), "geo-shape")) == 2,
    bad_coordinate: geo.validate({type: "Point", coordinates: [0, 100]}) is error,
    open_ring: geo.validate({type: "Polygon", coordinates: [[[0, 0], [10, 0], [10, 10], [0, 10]]]}) is error,
    crossed_ring: chart.render_spec({*:base, data: {type: "Polygon", coordinates: [[[0, 0], [20, 20], [0, 20], [20, 0], [0, 0]]]}}) is error,
    bad_collection: chart.render_spec({*:base, data: {type: "FeatureCollection"}}) is error,
    unknown_shape: geo.validate({type: "bad", coordinates: [0, 0]}) is error,
    unknown_projection: projection.configure(200, 200, "bad") is error,
    bad_scale: projection.configure(200, 200, {scale: 0}) is error,
    bad_precision: projection.configure(200, 200, {precision: -1}) is error,
    bad_parallels: projection.configure(200, 200, {type: "albers", parallels: [90, 30]}) is error
};
[for (label, passed in checks where passed != true) string(label)]
