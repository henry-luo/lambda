// Font-independent math output API and native/CLI adapters (D7.1.1, D7.2.8).
import latex: .format_latex
import ascii: .format_ascii
import mathml: .mathml

pub fn to_latex(ast) => latex.to_latex(ast)
pub fn to_ascii(ast) => ascii.to_ascii(ast)

pub fn format(ast, options = null) string^ {
    let valid_options = if (options != null and not (options is map))
        raise error("math format: options must be a map")
    let flavor = string(options.flavor or "latex");
    if (flavor == "mathml") lambda.sys.format(mathml.to_mathml(ast,options)^,"xml")^
    else if (flavor == "ascii") to_ascii(ast)
    else if (flavor == "typst") raise error("math format: Typst math output is not implemented")
    else to_latex(ast)
}

pub fn format_native(ast, flavor, options = null) string^ =>
    format(ast,{*:if (options is map) options else {},flavor:flavor})^

pub fn format_file(path, options = null) string^ {
    let ast = input(path,{type:options.input_type or "math",flavor:options.input_flavor})^;
    format(ast,options)^
}
