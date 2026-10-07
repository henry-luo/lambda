// Corpus records separate script semantics from actual exporter success (S7.4.1–S7.4.4).
import latex: lambda.latex.latex
import util: lambda.latex.util
import html: lambda.latex.to_html

pub let SAMPLES = ["algebra", "arithmetic_lualatex", "biblatex_biber", "booktabs",
    "bungee_model", "complex_numbers", "diagonal_table_cells", "dragon_curve",
    "filter_overview", "foray_into_latex", "greek_russian", "longtable",
    "math_proof", "math_test_flight", "nursing_care_plan", "ofdm_spectrum",
    "plotting_lesson", "pulley_systems", "russian_article", "scientific_writing",
    "semantic_inference_rules", "sequent_calculus", "slashbox",
    "spacetime_diagrams", "thai_polyglossia", "tikzpeople", "timeline",
    "ubc_math_220", "unit_circle", "vietnamese_document", "xltabular"]

pub fn record(file, target, include_html = false) {
    let ast = input("test/latex/samples/" ++ file ++ ".tex", "latex") ^ { null }
    if (ast == null) {file: file, target: target, parsed: false}
    else {
        let result = latex.render_result(ast, {standalone: true,
            base_uri: "test/latex/samples", target: target})
        let serialization = html.to_html(result.elements) ^ { {message: ^.message} }
        let serialized = if (serialization is string) serialization else ""
        let visible = trim(replace(util.text_of(result.body), "\n", " "))
        {file: file, target: target, parsed: true,
         packages: [for (entry in result.packages) entry.key],
         diagnostics: result.diagnostics,
         unsupported: len([for (issue in result.diagnostics
             where issue.code == "unsupported-output") issue]),
         resources: result.assets,
         visible: slice(visible, 0, min([len(visible), 120])),
         output_chars: len(serialized), serialization_error: if (serialization is map) serialization.message else null,
         html: if (include_html) serialized else null}
    }
}
