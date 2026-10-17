// presence checks preserve authored nulls and empty values (S8.2.2v5).
import parse: lambda.chart.parse
import util: lambda.chart.util
import locale: .locale

pub fn attrs(value) => map([for (key, item in parse.attributes(value)) (string(key), item)])
pub fn has(value, key) => parse.has_attribute(value, key)
pub fn option(value, key, fallback) => if (has(value, key)) value[key] else fallback
pub fn text(value) string => if (value == null) "" else string(value)
pub fn items(value) => if (value == null) [] else if (value is array) value else [value]
pub fn contents(value) => if (value is element) content(value) else items(value)
pub fn tag(value) => if (value is element) replace(string(name(value)), "timeline.", "") else ""
pub fn fail(path, message) error => error("timeline: " ++ path ++ ": " ++ message)
pub fn positive(value) => util.finite_number(value) and value > 0
pub fn integral(value) => util.finite_number(value) and floor(value) == value
pub fn enum(value, choices) => contains(choices, text(value)) ^ { false }
pub fn id(value) string | error => if ((value is string or value is symbol) and len(string(value)) > 0) string(value)
    else fail("id", "expected a nonempty string or symbol")
pub fn properties(value, allowed, path, ordinary = false) {
    let bad = [for (key, item in attrs(value), let field = string(key) where not contains(allowed, field) and
        not (ordinary and (contains(["id", "class", "style", "role", "tabindex"], field) or
            starts_with(field, "aria-") or starts_with(field, "data-")))) field];
    if (len(bad) > 0) fail(path ++ "." ++ bad[0], "unknown attribute") else null
}
pub fn attributes(value) => map([for (key, item in attrs(value), let field = string(key) where
    contains(["id", "class", "style", "role", "tabindex"], field) or
    starts_with(field, "aria-") or starts_with(field, "data-")) (key, item)])
pub fn render(value) => if (value is array) [for (item in value) render(item)]
    else if (value is element) apply(value)
    else if (value is number or value is symbol or value is bool) string(value) else value
pub fn plain(value) => if (value is element) join(content(value) |> plain(~)," ")
    else if (value is array) join(value |> plain(~)," ") else if (value is map) plain(value.content) else text(value)

fn interpolate(message, values, language, index = 0) => if (index>=len(values)) message
    else interpolate(replace(message,"{" ++ values[index].key ++ "}",
        if (values[index].value is number) locale.format_number(values[index].value,language) else text(values[index].value)),values,language,index+1)
pub fn message(spec, key, fallback, parameters = {}) {
    let supplied=option(attrs(spec.messages),key,locale.message(spec.locale,key,fallback));
    let value=if (supplied is map) option(supplied,locale.plural(spec.locale,parameters.count),option(supplied,"other",fallback)) else supplied;
    if (value is fn) value(parameters) else interpolate(text(value),[for (field,item in attrs(parameters)) {key:string(field),value:item}],spec.locale)
}
