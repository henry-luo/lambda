import tr: lambda.chart.transform
import chart: lambda.chart.chart
import vega: lambda.chart.vega

fn run(data, step) => tr.apply_transforms(data, [step])
fn close(a, b) => abs(float(a) - float(b)) < 1e-7

let line = [for (x in 0 to 4) {x: x, y: 2 + 3 * x}]
let linear = run(line, {type: "regression", x: "x", y: "y", params: true})
let noisy = run([{x: 0, y: 1}, {x: 1, y: 2}, {x: 2, y: 1}, {x: 3, y: 3}, {x: 4, y: 5}],
    {type: "regression", x: "x", y: "y", params: true})
let quadratic = run([for (x in -2 to 2) {x: x, y: 1 + 2 * x + 3 * x * x}],
    {type: "regression", x: "x", y: "y", method: "quad", params: true})
let polynomial = run([for (x in -3 to 3) {x: x, y: 1 - 2 * x + 0.5 * x ** 2 + 4 * x ** 3}],
    {type: "regression", x: "x", y: "y", method: "poly", order: 3, params: true})
let logarithmic = run([for (x in 1 to 5) {x: x, y: 2 + 3 * math.log(x)}],
    {type: "regression", x: "x", y: "y", method: "log", params: true})
let exponential = run([for (x in 0 to 4) {x: x, y: 2 * math.exp(0.5 * x)}],
    {type: "regression", x: "x", y: "y", method: "exp", params: true})
let power = run([for (x in 1 to 5) {x: x, y: 3 * x ** 2}],
    {type: "regression", x: "x", y: "y", method: "pow", params: true})
let constant = run([{x: 1, y: 7}, {x: 2, y: 7}],
    {type: "regression", x: "x", y: "y", method: "poly", order: 0, params: true})
let large_predictor = run([for (offset in -3 to 3) {x: 1e9 + offset, y: 1 + 2 * offset + offset ** 2}],
    {type: "regression", x: "x", y: "y", method: "quad", extent: [1e9 - 2, 1e9 + 2], steps: 3})
let grouped = run([{g: "A", x: 1, y: 3}, {g: "B", x: 1, y: 5},
    {g: "A", x: 2, y: 5}, {g: "B", x: 2, y: 8}],
    {type: "regression", x: "x", y: "y", groupby: ["g"], extent: [0, 3], as: ["u", "v"]})
let smoothed = run(line, {type: "loess", x: "x", y: "y", bandwidth: 1.0})
let pair = run([{x: 1, y: 3}, {x: 2, y: 5}], {type: "loess", x: "x", y: "y"})
let spike = run([{x: 0, y: 0}, {x: 1, y: 0}, {x: 2, y: 100}, {x: 3, y: 0}, {x: 4, y: 0}],
    {type: "loess", x: "x", y: "y", bandwidth: 1.0})
let repeated = run([{x: 1, y: 2}, {x: 1, y: 4}], {type: "loess", x: "x", y: "y"})
let density = run([{v: -1}, {v: 1}, {v: null}, {v: inf}],
    {type: "density", field: "v", bandwidth: 1, extent: [-2, 2], steps: 5})
let cdf = run([{v: -1}, {v: 1}],
    {type: "density", field: "v", bandwidth: 1, extent: [-2, 2], steps: 5, cumulative: true})
let counts = run([{v: -1}, {v: 1}],
    {type: "density", field: "v", bandwidth: 1, extent: [-2, 2], steps: 5, counts: true})
let shared = run([{g: "A", v: -1}, {g: "A", v: 1}, {g: "B", v: 0}],
    {type: "density", field: "v", groupby: ["g"], resolve: "shared", steps: 3, as: ["x", "y"]})
let automatic = run([{v: 7}, {v: 7}], {type: "density", field: "v"})
let adapted = vega.convert({data: {values: line}, transform: [{regression: "y", on: "x", params: true}], mark: "line"})
let adapted_data = tr.apply_transforms(line, adapted.transform)
let plot = chart.render_spec(vega.convert({width: 300, height: 200, data: {values: line},
    transform: [{regression: "y", on: "x", extent: [0, 5]}], mark: "line",
    encoding: {x: {field: "x", type: "quantitative"}, y: {field: "y", type: "quantitative"}}}))
let checks = {
    linear_intercept: close(linear[0].coef[0], 2), linear_slope: close(linear[0].coef[1], 3),
    linear_fit: close(linear[0].r_squared, 1),
    noisy_coefficients: close(noisy[0].coef[0], 0.6) and close(noisy[0].coef[1], 0.9),
    noisy_fit: close(noisy[0].r_squared, 81.0 / 112.0),
    quadratic_coefficients: close(quadratic[0].coef[0], 1) and close(quadratic[0].coef[1], 2) and close(quadratic[0].coef[2], 3),
    polynomial_coefficients: close(polynomial[0].coef[0], 1) and close(polynomial[0].coef[1], -2) and
        close(polynomial[0].coef[2], 0.5) and close(polynomial[0].coef[3], 4),
    logarithmic_fit: close(logarithmic[0].coef[0], 2) and close(logarithmic[0].coef[1], 3),
    exponential_fit: close(exponential[0].coef[0], 2) and close(exponential[0].coef[1], 0.5),
    power_fit: close(power[0].coef[0], 3) and close(power[0].coef[1], 2),
    constant_fit: close(constant[0].coef[0], 7) and close(constant[0].r_squared, 1),
    centered_predictor: close(large_predictor[0].y, 1) and close(large_predictor[1].y, 1) and close(large_predictor[2].y, 9),
    groups: len(grouped) == 4 and grouped[0].g == "A" and grouped[2].g == "B",
    extrapolation: close(grouped[0].v, 1) and close(grouped[1].v, 7) and close(grouped[3].v, 11),
    loess_line: len(smoothed) == 5 and len([for (row in smoothed where not close(row.y, 2 + 3 * row.x)) row]) == 0,
    loess_pair: close(pair[0].y, 3) and close(pair[1].y, 5),
    loess_smoothing: spike[2].y > 0 and spike[2].y < 100,
    repeated_predictor: len(repeated) == 1 and close(repeated[0].y, 3),
    density_grid: (density |> ~.value) == [-2, -1, 0, 1, 2],
    gaussian_density: close(density[2].density, 0.24197072451914337),
    density_symmetry: close(density[0].density, density[4].density),
    density_cdf: close(cdf[2].density, 0.5) and cdf[0].density < cdf[1].density and cdf[3].density < cdf[4].density,
    density_counts: close(counts[2].density, 2 * density[2].density),
    density_shared_grid: len(shared) == 6 and shared[0].x == shared[3].x and shared[2].x == shared[5].x,
    constant_density: len(automatic) == 1 and automatic[0].density > 0 and automatic[0].density < inf,
    vega_model_params: close(adapted_data[0].rSquared, 1),
    svg_line: plot is element and len(content(plot[1][0])) == 1,
    empty_density: run([], {type: "density", field: "v"}) == [],
    empty_regression: run([], {type: "regression", x: "x", y: "y"}) == [],
    empty_loess: run([], {type: "loess", x: "x", y: "y"}) == [],
    bad_bandwidth: run(line, {type: "density", field: "x", bandwidth: -1}) is error,
    bad_steps: run(line, {type: "density", field: "x", steps: 1}) is error,
    bad_extent: run(line, {type: "density", field: "x", extent: [5, 1]}) is error,
    bad_method: run(line, {type: "regression", x: "x", y: "y", method: "unknown"}) is error,
    bad_order: run(line, {type: "regression", x: "x", y: "y", method: "poly", order: -1}) is error,
    insufficient_data: run([{x: 1, y: 2}], {type: "regression", x: "x", y: "y"}) is error,
    deficient_predictor: run([{x: 1, y: 2}, {x: 1, y: 3}], {type: "regression", x: "x", y: "y"}) is error,
    bad_loess: run(line, {type: "loess", x: "x", y: "y", bandwidth: 0}) is error,
    vega_density: len(tr.apply_transforms(line, vega.convert({transform: [{density: "x", steps: 3}]}).transform)) == 3,
    vega_loess: len(tr.apply_transforms(line, vega.convert({transform: [{loess: "y", on: "x"}]}).transform)) == 5
};
[for (label, passed in checks where passed != true) string(label)]
