// Two-dimensional Gaussian density and collision-free one-axis point displacement.
import util: .util
import parse: .parse
import geometry: .geometry
import mark: .mark
import color: .color
import svg: .svg
import scale: .scale

fn positive(value) => util.finite_number(value) and value > 0
fn grid_count(value) => positive(value) and floor(value) == value

pub fn density(data, options = {}) {
    let xf = if (options.x != null) options.x else "x";
    let yf = if (options.y != null) options.y else "y";
    let extent = if (options.extent != null) options.extent else
        [for (field in [xf, yf], let values = data |> ~[field])
            if (len(values) == 0) [0.0, 1.0] else if (min(values) == max(values)) util.nice_domain(min(values), max(values))
            else [min(values), max(values)]];
    let count = if (options.resolution is array) options.resolution else
        [if (options.resolution != null) options.resolution else 32, if (options.resolution != null) options.resolution else 32];
    let bandwidth = if (options.bandwidth is array) options.bandwidth else
        [if (options.bandwidth != null) options.bandwidth else (extent[0][1] - extent[0][0]) / 10.0,
            if (options.bandwidth != null) options.bandwidth else (extent[1][1] - extent[1][0]) / 10.0];
    let invalid = util.first_error([
        if (not (count is array) or len(count) != 2 or not all(count |> grid_count(~))) error("chart: density resolution requires two positive integers"),
        if (not (bandwidth is array) or len(bandwidth) != 2 or not all(bandwidth |> positive(~))) error("chart: density bandwidth must be positive"),
        if (not (extent is array) or len(extent) != 2 or any([for (bounds in extent) not (bounds is array) or
            len(bounds) != 2 or not all(bounds |> util.finite_number(~)) or bounds[0] >= bounds[1]])) error("chart: density requires increasing finite x/y extents"),
        if (any([for (row in data) not util.finite_number(row[xf]) or not util.finite_number(row[yf])])) error("chart: density requires finite x/y values")]);
    if (invalid is error) invalid else {
        let cells = [for (j in 0 to (int(count[1]) - 1)) for (i in 0 to (int(count[0]) - 1),
            let x0 = util.lerp(extent[0][0], extent[0][1], float(i) / count[0]),
            let x1 = util.lerp(extent[0][0], extent[0][1], float(i + 1) / count[0]),
            let y0 = util.lerp(extent[1][0], extent[1][1], float(j) / count[1]),
            let y1 = util.lerp(extent[1][0], extent[1][1], float(j + 1) / count[1]),
            let x = (x0 + x1) / 2.0, let y = (y0 + y1) / 2.0)
            {x: x, y: y, x0: x0, x1: x1, y0: y0, y1: y1, i: i, j: j,
                density: if (len(data) == 0) 0.0 else sum([for (row in data)
                    math.exp(-0.5 * (((x - row[xf]) / bandwidth[0]) ** 2 + ((y - row[yf]) / bandwidth[1]) ** 2))]) /
                    (float(len(data)) * util.TAU * bandwidth[0] * bandwidth[1])}];
        {cells: cells, resolution: count, bandwidth: bandwidth, extent: extent}
    }
}

// Linear triangles resolve saddle cells consistently and preserve holes in filled level sets.
pub fn contours(grid, levels = null) {
    let count = grid.resolution;
    let top = max([0.0, for (cell in grid.cells) cell.density]);
    let thresholds = if (levels is array) levels else [for (i in 1 to (if (levels != null) int(levels) else 8))
        top * float(i) / float((if (levels != null) int(levels) else 8) + 1)];
    if (grid is error) grid
    else if (not (levels is array) and levels != null and not grid_count(levels)) error("chart: density levels require a positive integer or finite thresholds")
    else if (not all(thresholds |> util.finite_number(~))) error("chart: density contour thresholds must be finite")
    else [for (level in thresholds) {level: level, polygons: [for (j in 0 to (int(count[1]) - 2))
        for (i in 0 to (int(count[0]) - 2),
            let a = grid.cells[j * count[0] + i], let b = grid.cells[j * count[0] + i + 1],
            let c = grid.cells[(j + 1) * count[0] + i + 1], let d = grid.cells[(j + 1) * count[0] + i])
            for (triangle in [[a, b, c], [a, c, d]],
                let points = [for (cell in triangle) [cell.x, cell.y, cell.density]],
                let clipped = geometry.clip(points, (point) => point[2] - level) where len(clipped) >= 3) clipped]}]
}

fn swarm_points(points, padding, index = 0, placed = []) {
    if (index >= len(points)) placed else {
        let point = points[index];
        let candidates = [0.0, for (other in placed,
            let delta = point.measure - other.measure,
            let radius = point.radius + other.radius + padding,
            let squared = radius * radius - delta * delta where squared >= 0)
            for (side in [-1.0, 1.0]) other.offset + side * math.sqrt(squared)];
        let valid = [for (offset in candidates where all([for (other in placed)
            (point.measure - other.measure) ** 2 + (offset - other.offset) ** 2 + 0.000000001 >=
                (point.radius + other.radius + padding) ** 2])) offset];
        let offset = sort(valid, (value) => abs(value))[0];
        swarm_points(points, padding, index + 1, [*placed, {*:point, offset: offset}])
    }
}

pub fn beeswarm(data, ctx, options = {}) {
    let axis = if (options.axis != null) options.axis else "x";
    let measure = if (axis == "x") "y" else "x";
    let padding = if (options.padding != null) options.padding else 1.0;
    let field = ctx.encoding[axis].field;
    let groups = util.unique_vals(data |> ~[field]);
    let points = [for (i, row in data) {row: row, index: i, measure: mark.center_coordinate(ctx, measure, row),
        category: mark.center_coordinate(ctx, axis, row),
        radius: math.sqrt(mark.appearance(ctx, "size", row, if (options.size != null) options.size else 30.0) / util.PI)}];
    if (not contains(["x", "y"], axis) or not contains(["band", "point"], ctx[axis ++ "_scale"].kind) or
        not util.finite_number(padding) or padding < 0) error("chart: beeswarm requires a categorical axis and nonnegative padding")
    else if (any([for (point in points) not util.finite_number(point.measure) or not util.finite_number(point.radius)]))
        error("chart: beeswarm requires finite positions and nonnegative point sizes")
    else [for (group in groups) for (point in swarm_points(points |: ~.row[field] == group, padding))
        {*:point, x: if (axis == "x") point.category + point.offset else point.measure,
            y: if (axis == "x") point.measure else point.category + point.offset}]
}

pub fn render_swarm(data, ctx, options) {
    let points = beeswarm(data, ctx, options);
    if (points is error) points else svg.group_class("marks beeswarm", [for (point in points)
        <circle cx: point.x, cy: point.y, r: point.radius,
            *:mark.style(ctx, point.row, options, {fill: color.default_color, opacity: 1.0}), mark.tooltip(ctx, point.row)>])
}

// Density glyphs expose derived records and stable grid/level identities, never an arbitrary input row.
pub fn prepare(spec) {
    let options=spec.mark;
    let grid=density(spec.data,{*:options,x:spec.encoding.x.field,y:spec.encoding.y.field});
    let levels=if (grid is error) grid else if (options.contours==true or options.mode=="contour") contours(grid,options.levels) else null;
    let failure=util.first_error([grid,levels]);
    let rows=if (levels!=null) levels else grid.cells;
    let derived=[for (row in rows,
        let key=if (levels!=null) "level:"++string(row.level) else "cell:"++string(row.i)++":"++string(row.j))
        {*:row,id:key,source_rows:spec.data,*:map([for (field in [spec.encoding.key.field] where field!=null) for (part in [field,key]) part]),
            *:if (levels==null) map([spec.encoding.x.field,row.x,spec.encoding.y.field,row.y]) else {}}];
    {*:spec,data:if (failure is error) failure else derived,_domain_data:spec.data,_density:grid,_density_contours:levels!=null}
}
pub fn render_density(data, ctx, options) {
    let prepared=if (ctx._density!=null) {data:data,_density:ctx._density,_density_contours:ctx._density_contours}
        else prepare({data:data,encoding:ctx.encoding,mark:options});
    let grid=prepared._density;
    let maximum=if (grid is error) 0.0 else max([0.0,for (cell in grid.cells) cell.density]);
    let context={*:ctx,data:prepared.data};
    if (prepared.data is error) prepared.data else svg.group_class("marks density", [for (row in prepared.data,
        let fill=color.sequential_color(color.get_scheme("blues"),if (maximum>0) (if (prepared._density_contours) row.level else row.density)/maximum else 0.0),
        let appearance=mark.style(context,row,options,{fill:fill,stroke:"none",opacity:1.0}))
        if (prepared._density_contours) <path class:"density-contour",'data-density':row.level,
            d:join([for (polygon in row.polygons) svg.line_path([for (point in polygon)
                [scale.scale_apply(ctx.x_scale,point[0]),scale.scale_apply(ctx.y_scale,point[1])]])++" Z"]," "),
            *:appearance,mark.tooltip(context,row)>
        else (
            let xs=[scale.scale_apply(ctx.x_scale,row.x0),scale.scale_apply(ctx.x_scale,row.x1)],
            let ys=[scale.scale_apply(ctx.y_scale,row.y0),scale.scale_apply(ctx.y_scale,row.y1)],
            <rect class:"density-cell",x:min(xs),y:min(ys),width:abs(xs[1]-xs[0]),height:abs(ys[1]-ys[0]),
                *:appearance,mark.tooltip(context,row)>)])
}
