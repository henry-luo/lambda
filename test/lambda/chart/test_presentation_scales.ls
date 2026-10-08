import util: lambda.chart.util
import scale: lambda.chart.scale
import chart: lambda.chart.chart
import color: lambda.chart.color
import vega: lambda.chart.vega

let rows = [{category: "B", value: 3}, {category: "A", value: 2}, {category: "B", value: 4}, {category: "C", value: 2}]
let categorical = scale.position_scale({field: "category", dtype: "nominal",
    sort: {field: "value", op: "sum", order: "descending"}}, rows, 0, 100, "bar", true)
let diverging = scale.infer_color_scale({field: "value", dtype: "quantitative",
    scale: {domain: [-10, 30], domain_mid: 0}}, rows)
let explicit = scale.infer_color_scale({field: "value", dtype: "quantitative",
    scale: {domain: [-10, 0, 30], range: ["red", "white", "blue"]}}, rows)
let reversed = scale.infer_color_scale({field: "value", dtype: "quantitative",
    scale: {domain: [30, -10], domain_mid: 0, reverse: true, range: ["red", "white", "blue"]}}, rows)
let image = chart.render_spec(vega.convert({width: 300, height: 200, data: {values: rows}, mark: "bar", encoding: {
    x: {field: "category", type: "nominal", sort: {field: "value", op: "sum", order: "descending"}, axis: null},
    y: {field: "value", type: "quantitative", aggregate: "sum", axis: null}}}))
let checks = {
    fixed: util.format_value(12.5, ".2f") == "12.50",
    percentage: util.format_value(-0.125, ".1%") == "-12.5%",
    grouped: util.format_value(12345.678, ",.2f") == "12,345.68",
    wide: util.format_value(5000000000, ",.0f") == "5,000,000,000",
    large_fixed: util.format_value(1e20, ",.2f") == "100,000,000,000,000,000,000.00",
    subnormal: util.format_value(1e-310, ".3g") == "1.00e-310",
    accounting: util.format_value(-1234.5, "($,.2f") == "($1,234.50)",
    positive: util.format_value(12.5, "+.1f") == "+12.5",
    scientific: util.format_value(12345, ".2e") == "1.23e+4",
    scientific_carry: util.format_value(999.9, ".2e") == "1.00e+3",
    significant: util.format_value(12.345, ".3g") == "12.3",
    general_exponent: util.format_value(12345, ".3g") == "1.23e+4",
    rounded: util.format_value(12345, ".3r") == "12300",
    si: util.format_value(12345, ".3s") == "12.3k",
    si_small: util.format_value(0.000123, ".3s") == "123µ",
    si_carry: util.format_value(999999, ".3s") == "1.00M",
    trimmed: util.format_value(1000000, ".3~s") == "1M",
    integer: util.format_value(1234.2, ",d") == "1,234",
    fallback: util.format_value(12, ".2x") == "12",
    category_order: categorical.domain == ["B", "A", "C"],
    sort_aggregate: image[1][0][0].x < image[1][0][1].x and image[1][0][1].x < image[1][0][2].x,
    diverging_mid: diverging.domain == [-10, 0, 30] and scale.scale_apply(diverging, 0) == "#f7f7f7",
    diverging_bounds: scale.scale_apply(diverging, -10) == "#b2182b" and scale.scale_apply(diverging, 30) == "#2166ac",
    diverging_ticks: max(scale.scale_ticks(diverging, 4)) == 30,
    diverging_halves: scale.scale_apply(explicit, -5) == "white" and scale.scale_apply(explicit, 15) == "blue",
    reversed: scale.scale_apply(reversed, -10) == "red" and scale.scale_apply(reversed, 30) == "blue",
    ordinal_reverse: scale.scale_apply(scale.infer_color_scale({field: "category", dtype: "nominal",
        scale: {domain: ["A", "B"], range: ["red", "blue"], reverse: true}}, rows), "A") == "blue",
    invalid_mid: scale.infer_color_scale({field: "value", dtype: "quantitative",
        scale: {domain: [-1, 1], domain_mid: 2}}, rows) is error,
    palettes: color.get_scheme("viridis")[0] == "#440154" and color.get_scheme("viridis")[32] == "#fde725" and
        color.get_scheme("magma")[32] == "#fcfdbf" and color.get_scheme("inferno")[32] == "#fcffa4" and
        color.get_scheme("plasma")[32] == "#f0f921" and len(color.get_scheme("set2")) == 8
};
[for (label, passed in checks where passed != true) string(label)]
