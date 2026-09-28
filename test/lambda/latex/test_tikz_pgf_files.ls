// Auto-detected .pgf fragments render without a synthetic LaTeX document.
import tikz: lambda.doc.tikz.tikz
import expr: lambda.doc.tikz.expression

fn children_named(node, tag) => [for (child in node
    where child is element and string(name(child)) == tag) child]

let segmented = input("test/input/tikz/segmentada.pgf")^
let polar = input("test/input/tikz/polar.pgf")^
let logaxis = input("test/input/tikz/plot_logaxis.pgf")^
let series = input("test/input/tikz/serie_coordinate.pgf")^
let venn1 = input("test/input/tikz/diagrama_venn1.pgf")^
let venn2 = input("test/input/tikz/diagrama_venn2.pgf")^
let parametric = input("test/input/tikz/plot_parametric.pgf")^
let regression = input("test/input/tikz/reg_components.pgf")^
let segmented_picture = children_named(segmented, "tikzpicture")[0]
let segmented_axis = children_named(segmented_picture, "axis")[0]
let segmented_conditional = children_named(
    children_named(segmented_axis, "plot")[1], "conditional")[0]
let polar_picture = children_named(polar, "tikzpicture")[0]
let polar_axis = children_named(polar_picture, "polaraxis")[0]
let polar_plot = children_named(polar_axis, "plot")[0]
let log_pictures = children_named(logaxis, "tikzpicture")
let log_axis = children_named(log_pictures[0], "loglogaxis")[0]
let log_legends = children_named(log_axis, "legend_entry")
let series_axis = children_named(children_named(series, "tikzpicture")[0], "axis")[0]
let series_plot = children_named(series_axis, "plot")[0]
let venn1_picture = children_named(venn1, "tikzpicture")[0]
let venn1_paths = children_named(venn1_picture, "path")
let venn2_picture = children_named(venn2, "tikzpicture")[0]
let venn2_paths = children_named(venn2_picture, "path")
let parametric_axis = children_named(children_named(parametric, "tikzpicture")[0], "axis")[0]
let parametric_plot = children_named(parametric_axis, "plot")[0]
let regression_axis = children_named(children_named(regression, "tikzpicture")[0], "axis")[0]
let regression_plots = children_named(regression_axis, "plot")
let regression_paths = children_named(regression_axis, "path")
let segmented_html = format(tikz.render_document(segmented, null)^, 'xml')
let polar_html = format(tikz.render_document(polar, null)^, 'xml')
let log_html = format(tikz.render_document(logaxis, null)^, 'xml')
let series_html = format(tikz.render_document(series, {text_width_px: 1200})^, 'xml');
let venn1_html = format(tikz.render_document(venn1, null)^, 'xml')
let venn2_html = format(tikz.render_document(venn2, null)^, 'xml');
let parametric_html = format(tikz.render_document(parametric, null)^, 'xml');
let regression_html = format(tikz.render_document(regression, null)^, 'xml');

[
    string(name(segmented)) == "tikz_picture",
    len(children_named(segmented, "tikzpicture")) == 1,
    expr.evaluate(segmented_conditional, 1.0)^ == 1.0,
    expr.evaluate(segmented_conditional, 3.0)^ == -1.0,
    contains(segmented_html, "tikz-axis") and contains(segmented_html, "green"),
    len(children_named(polar, "pgfplots_library")) == 1,
    expr.evaluate(children_named(polar_plot, "function_call")[0], 90.0)^ == 1.0,
    contains(polar_html, "tikz-polar-axis") and contains(polar_html, "Polar coordinates"),
    len(log_pictures) == 2,
    len(log_legends) == 2 and log_legends[0].source == "Case 1",
    contains(log_html, "tikz-fragment-gallery") and contains(log_html, "Case 1"),
    len(children_named(series_plot, "point")) == 25,
    contains(series_html, "width=\"960\"") and contains(series_html, "height=\"720\""),
    contains(series_html, "Number of failed banks") and contains(series_html, "tikz-axis"),
    len(venn1_paths) == 3 and venn1_paths[0].shape == "rectangle" and
        venn1_paths[1].shape == "ellipse",
    len(children_named(venn1_paths[2], "point")) == 33,
    contains(venn1_html, "<ellipse") and contains(venn1_html, "<rect") and
        contains(venn1_html, "tikz-label"),
    len(venn2_paths) == 9 and len([for (path in venn2_paths
        where path.shape == "ellipse") path]) == 1,
    contains(venn2_html, "<ellipse") and contains(venn2_html, "darkgreen"),
    len(children_named(parametric, "pgfplots_setting")) == 1,
    parametric_plot.input_kind == "parametric" and
        len(children_named(parametric_plot, "x_expression")) == 1 and
        len(children_named(parametric_plot, "y_expression")) == 1,
    len([for (option in children_named(parametric_axis, "option")
        where option.key == "axis x line" and option.value == "middle") option]) == 1,
    contains(parametric_html, "tikz-axis") and contains(parametric_html, "stroke=\"blue\""),
    len(regression_plots) == 2 and
        len(children_named(regression_plots[0], "node")) == 1,
    len(children_named(regression_axis, "coordinate")) == 3 and
        len(regression_paths) == 2,
    contains(regression_html, "Componente") and
        not contains(regression_html, "left:nullpx") and
        not contains(regression_html, "<path d=\"\""),
    contains(regression_html, " C")
]
