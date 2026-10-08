// Pure analytical transforms emit records suitable for ordinary line/area encodings.
import records: .records
import util: .util

fn output_names(step, defaults) => if (step.as != null) step.as else defaults

fn valid_names(names) => names is array and len(names) == 2 and names[0] is string and names[1] is string

fn valid_extent(extent) => extent == null or (extent is array and len(extent) == 2 and
    util.finite_number(extent[0]) and util.finite_number(extent[1]) and extent[0] <= extent[1])

fn sample_grid(extent, steps) => [for (i in 0 to (steps - 1))
    util.lerp(extent[0], extent[1], float(i) / float(steps - 1))]

fn samples(extent, steps) => if (extent[0] == extent[1]) [extent[0]] else sample_grid(extent, steps)

fn output_row(group, names, x, y) => records.add_field(records.add_field(group, names[0], x), names[1], y)

fn collect(groups) {
    let failure = util.first_error(groups);
    if (failure is error) failure else [for (group in groups) for (row in group) row]
}

fn positive_integer(value) => util.finite_number(value) and value >= 1 and floor(value) == value

pub fn density(data, step) {
    let names = output_names(step, ["value", "density"]);
    let bandwidth = if (step.bandwidth != null) step.bandwidth else 0.0;
    let minsteps = if (step.minsteps != null) step.minsteps else 25;
    let maxsteps = if (step.maxsteps != null) step.maxsteps else 200;
    let count = if (step.steps != null) step.steps else max([minsteps, min([maxsteps, 100])]);
    let fields = if (step.groupby != null) step.groupby else [];
    let valid = data |: util.finite_number(~[step.field]);
    let shared_extent = if (step.extent != null) step.extent
        else if (step.resolve == "shared" and len(valid) > 0) util.field_extent(valid, step.field) else null;
    if (step.field == null or not (fields is array) or not valid_names(names) or
        not util.finite_number(bandwidth) or bandwidth < 0 or not valid_extent(step.extent) or
        not positive_integer(count) or count < 2 or not positive_integer(minsteps) or
        not positive_integer(maxsteps) or minsteps > maxsteps)
        error("chart: invalid density field, bandwidth, extent, steps, or output fields")
    else collect([for (partition in records.group_by(valid, fields),
        let values = partition.rows |> float(~[step.field]),
        let h = if (bandwidth > 0) float(bandwidth) else automatic_bandwidth(values),
        let extent = if (shared_extent != null) shared_extent else util.extent(values),
        let group = records.group_fields(partition.rows[0], fields))
        [for (x in samples(extent, int(count)),
            let probability = avg([for (value in values)
                if (step.cumulative == true) normal_cdf((x - value) / h)
                else math.exp(-0.5 * ((x - value) / h) ** 2) / (h * math.sqrt(util.TAU))]))
            output_row(group, names, x, probability * (if (step.counts == true) float(len(values)) else 1.0))]])
}

fn automatic_bandwidth(values) {
    let n = len(values);
    let sigma = math.sqrt(math.variance(values) * (if (n > 1) float(n) / float(n - 1) else 1.0));
    let iqr = (math.quantile(values, 0.75) - math.quantile(values, 0.25)) / 1.34;
    let spread = if (sigma > 0 and iqr > 0) min([sigma, iqr]) else if (sigma > 0) sigma
        else if (abs(values[0]) > 0) abs(values[0]) else 1.0;
    1.06 * spread * float(n) ** -0.2
}

// Symmetry avoids subtracting a tiny upper-tail probability from itself.
fn normal_cdf(z) {
    let x = abs(z);
    let t = 1.0 / (1.0 + 0.2316419 * x);
    let tail = math.exp(-0.5 * x * x) / math.sqrt(util.TAU) * t *
        (0.319381530 + t * (-0.356563782 + t * (1.781477937 + t * (-1.821255978 + t * 1.330274429))));
    if (z < 0) tail else 1.0 - tail
}

fn method_name(method) {
    if (method == null) "linear"
    else if (method == "polynomial") "poly"
    else if (method == "quadratic") "quad"
    else if (method == "exponential") "exp"
    else if (method == "logarithmic") "log"
    else if (method == "power") "pow"
    else method
}

pub fn regression(data, step) {
    let method = method_name(step.method);
    let degree = if (method == "quad") 2 else if (method == "poly")
        (if (step.order != null) step.order else 3) else 1;
    let fields = if (step.groupby != null) step.groupby else [];
    let names = output_names(step, [step.x, step.y]);
    let count = if (step.steps != null) step.steps else if (method == "linear") 2 else 100;
    if (step.x == null or step.y == null or not (fields is array) or not valid_names(names) or
        not contains(["linear", "log", "exp", "pow", "quad", "poly"], method) or
        not util.finite_number(degree) or degree < 0 or floor(degree) != degree or
        not valid_extent(step.extent) or not positive_integer(count) or count < 2 or
        (contains(["log", "pow"], method) and step.extent != null and step.extent[0] <= 0))
        error("chart: invalid regression fields, method, order, extent, or steps")
    else {
        let valid = data |: util.finite_number(~[step.x]) and util.finite_number(~[step.y]) and
            (not contains(["log", "pow"], method) or ~[step.x] > 0) and
            (not contains(["exp", "pow"], method) or ~[step.y] > 0);
        collect([for (partition in records.group_by(valid, fields))
            regression_group(partition.rows, step, method, int(degree), names, fields, int(count))])
    }
}

fn regression_group(rows, step, method, degree, names, fields, count) {
    let points = [for (row in rows) {
        x: if (method == "log" or method == "pow") math.log(float(row[step.x])) else float(row[step.x]),
        y: if (method == "exp" or method == "pow") math.log(float(row[step.y])) else float(row[step.y])}];
    let fit = least_squares(points, degree);
    let group = records.group_fields(rows[0], fields);
    if (fit is error) fit
    else if (step.params == true) {
        let actual = rows |> float(~[step.y]);
        let predicted = [for (row in rows) predict(fit, method, float(row[step.x]))];
        let residual = sum([for (index, value in actual) (value - predicted[index]) ** 2]);
        let total = sum([for (value in actual) (value - avg(actual)) ** 2]);
        let coef = original_coefficients(fit);
        [records.add_field({*:group, coef: if (method == "exp" or method == "pow")
            [math.exp(coef[0]), coef[1]] else coef},
            if (step.r_squared_name != null) step.r_squared_name else "r_squared",
            if (total == 0) (if (residual < 1e-20) 1.0 else 0.0) else 1.0 - residual / total)]
    } else {
        let extent = if (step.extent != null) step.extent else util.field_extent(rows, step.x);
        [for (x in samples(extent, count)) output_row(group, names, x, predict(fit, method, x))]
    }
}

fn predict(fit, method, x) {
    let predictor = if (method == "log" or method == "pow") math.log(x) else x;
    let value = polynomial(fit.coef, (predictor - fit.center) / fit.span);
    if (method == "exp" or method == "pow") math.exp(value) else value
}

fn polynomial(coef, x) => sum([for (index, value in coef) value * x ** index])

// Center/scale the predictor and use QR rather than squaring the condition number with normal equations.
fn least_squares(points, degree) {
    let extent = util.field_extent(points, "x");
    let center = (extent[0] + extent[1]) / 2.0;
    let span = if (extent[1] > extent[0]) (extent[1] - extent[0]) / 2.0 else 1.0;
    let columns = [for (power in 0 to degree) [for (point in points) ((point.x - center) / span) ** power]];
    let qr = qr_columns(columns, 0, [], []);
    if (len(points) <= degree) error("chart: regression needs more observations than its polynomial order")
    else if (qr is error) qr
    else {
        let rhs = [for (column in qr.q) sum([for (index, value in column) value * points[index].y])];
        {coef: back_substitute(qr.r, rhs, degree, []), center: center, span: span}
    }
}

fn qr_columns(columns, index, basis, triangular) {
    if (index >= len(columns)) {q: basis, r: triangular}
    else {
        let projection = orthogonalize(columns[index], basis, 0, []);
        let norm = math.sqrt(sum([for (value in projection.vector) value * value]));
        if (norm <= 1e-12 * math.sqrt(float(len(columns[index])))) error("chart: regression predictor is rank deficient")
        else qr_columns(columns, index + 1,
            [*basis, projection.vector |> ~ / norm], [*triangular, [*projection.coefficients, norm]])
    }
}

fn orthogonalize(vector, basis, index, coefficients) {
    if (index >= len(basis)) {vector: vector, coefficients: coefficients}
    else {
        let projection = sum([for (i, value in vector) value * basis[index][i]]);
        orthogonalize([for (i, value in vector) value - projection * basis[index][i]],
            basis, index + 1, [*coefficients, projection])
    }
}

fn back_substitute(columns, rhs, index, result) {
    if (index < 0) result
    else {
        let remaining = sum([for (offset, value in result) value * columns[index + offset + 1][index]]);
        back_substitute(columns, rhs, index - 1, [(rhs[index] - remaining) / columns[index][index], *result])
    }
}

fn choose(n, k) => if (k == 0) 1.0 else choose(n, k - 1) * float(n - k + 1) / float(k)

fn original_coefficients(fit) => [for (k in 0 to (len(fit.coef) - 1))
    sum([for (j in k to (len(fit.coef) - 1))
        fit.coef[j] * choose(j, k) * (-fit.center) ** (j - k) / fit.span ** j])]

pub fn loess(data, step) {
    let fields = if (step.groupby != null) step.groupby else [];
    let bandwidth = if (step.bandwidth != null) step.bandwidth else 0.3;
    let names = output_names(step, [step.x, step.y]);
    if (step.x == null or step.y == null or not (fields is array) or not valid_names(names) or
        not util.finite_number(bandwidth) or bandwidth <= 0 or bandwidth > 1)
        error("chart: loess requires x/y fields and bandwidth in (0, 1]")
    else {
        let valid = data |: util.finite_number(~[step.x]) and util.finite_number(~[step.y]);
        collect([for (partition in records.group_by(valid, fields),
            let rows = records.sort_rows(partition.rows, {field: step.x}),
            let group = records.group_fields(rows[0], fields),
            let points = [for (row in rows) {x: float(row[step.x]), y: float(row[step.y])}])
            [for (x in util.unique_vals(points |> ~.x))
                output_row(group, names, x, local_fit(points, x, bandwidth))]])
    }
}

fn local_fit(points, x, bandwidth) {
    let neighbors = sort(points, (point) => abs(point.x - x));
    let count = min([len(points), max([3, int(ceil(float(len(points)) * bandwidth))])]);
    let radius = abs(neighbors[count - 1].x - x);
    // Three neighbors leave an interior point after the tricube kernel zeroes its boundary.
    let distance = radius;
    let weighted = [for (point in points,
        let delta = point.x - x,
        let weight = if (len(points) <= 2) 1.0
            else if (distance == 0) (if (delta == 0) 1.0 else 0.0)
            else (if (abs(delta) >= distance) 0.0 else (1.0 - (abs(delta) / distance) ** 3) ** 3))
        {x: delta, y: point.y, w: weight}];
    let mass = sum(weighted |> ~.w);
    let mean_x = sum(weighted |> ~.w * ~.x) / mass;
    let mean_y = sum(weighted |> ~.w * ~.y) / mass;
    let variance = sum(weighted |> ~.w * (~.x - mean_x) ** 2);
    let covariance = sum(weighted |> ~.w * (~.x - mean_x) * (~.y - mean_y));
    if (variance == 0) mean_y else mean_y - covariance / variance * mean_x
}
