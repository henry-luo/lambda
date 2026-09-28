// Render complete documents that combine several TikZ paths or PGFPlots series.
import latex: lambda.latex.latex

fn rendered_ok(html, title, graphic_class, label) bool^ =>
    contains(html, title)^ and contains(html, graphic_class)^ and
    contains(html, label)^ and not contains(html, "latex-tikz-unsupported")^

let pipeline = input("test/input/tikz_release_pipeline.tex", {type: "latex"})^
let scaling = input("test/input/tikz_power_scaling.tex", {type: "latex"})^
let dose = input("test/input/tikz_dose_response.tex", {type: "latex"})^
let survey = input("test/input/tikz_field_survey.tex", {type: "latex"})^
let thermal = input("test/input/tikz_thermal_cycle.tex", {type: "latex"})^

let pipeline_html = latex.render_to_html(pipeline, {standalone: false})
let scaling_html = latex.render_to_html(scaling, {standalone: false})
let dose_html = latex.render_to_html(dose, {standalone: false})
let survey_html = latex.render_to_html(survey, {standalone: false})
let thermal_html = latex.render_to_html(thermal, {standalone: false});

[
    rendered_ok(pipeline_html, "Release Gate and Rollback Workflow",
        "tikz-named-picture", "Review defect")^,
    contains(pipeline_html, "data-radiant-layout=\"lambda-tikz\""),
    contains(format(pipeline, 'latex'), "\\draw[red,-{Latex}] (health) -- (rollback)"),
    rendered_ok(scaling_html, "Algorithm Scaling Study", "tikz-axis",
        "Quadratic reference")^,
    contains(scaling_html, "Observed counts") and contains(scaling_html, "<circle"),
    contains(format(scaling, 'latex'), "\\begin{loglogaxis}"),
    rendered_ok(dose_html, "Dose-Response Pilot Study", "tikz-axis",
        "Preparation B")^,
    contains(dose_html, "Replicate A") and contains(dose_html, "<circle"),
    contains(format(dose, 'latex'), "\\begin{semilogxaxis}"),
    rendered_ok(survey_html, "Field Survey Route Comparison", "tikz-picture",
        "Ridge sample")^,
    contains(survey_html, "River sample") and contains(survey_html, "Exit"),
    contains(format(survey, 'latex'), "\\draw[gray] (0,0) -- (9,0)"),
    rendered_ok(thermal_html, "Thermal Chamber Cycle", "tikz-axis",
        "Response model")^,
    contains(thermal_html, "Sensor samples") and contains(thermal_html, "<circle"),
    contains(format(thermal, 'latex'), "\\addplot[blue] {20+5*sin(x)}")
]
