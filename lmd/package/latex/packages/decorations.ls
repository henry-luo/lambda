// soul and ulem: underlines, strike-outs, highlights and letterspacing as CSS text
// decoration (vibe/impl/Lambda_Impl_Latex_Packages.md §7).
import util: ~~.util
import color: ~~.elements.color

// ulem's commands. \xout hatches its text, which CSS cannot draw: it strikes it
// through and is reported as an approximation (registry.target_diagnostics).
let ULEM = {uline: "text-decoration:underline", uuline: "text-decoration:underline double",
    uwave: "text-decoration:underline wavy", sout: "text-decoration:line-through",
    xout: "text-decoration:line-through", dashuline: "text-decoration:underline dashed",
    dotuline: "text-decoration:underline dotted"}

// soul's commands and their \text… aliases. A decoration colored by a \set…color
// setting names it; soul's highlight defaults to yellow, the others to the text color.
// \so spaces letters by soul's default .25em and words by .65em, which is .067em past
// a .333em space that also takes the letter spacing; \caps spaces small caps by .028em.
let SOUL = {
    hl: {css: "", colors: "background-color", setting: "sethlcolor", fallback: "yellow"},
    ul: {css: "text-decoration:underline", colors: "text-decoration-color", setting: "setulcolor"},
    st: {css: "text-decoration:line-through", colors: "text-decoration-color", setting: "setstcolor"},
    so: {css: "letter-spacing:0.25em;word-spacing:0.067em"},
    caps: {css: "font-variant:small-caps;letter-spacing:0.028em"}}
let SOUL_ALIASES = {texthl: "hl", textul: "ul", textst: "st", textso: "so", textcaps: "caps"}

pub let SOUL_COMMANDS = [for (k, v at SOUL) string(k)] ++ [for (k, v at SOUL_ALIASES) string(k)]
pub let SOUL_SETTINGS = ["sethlcolor", "setulcolor", "setstcolor"]
pub let ULEM_COMMANDS = [for (k, v at ULEM) string(k)]

fn decorated(command, css, items) {
    <span class: "latex-" ++ command, style: css, for c in items { c }>
}

pub fn render_ulem(el, items) {
    let command = string(name(el))
    decorated(command, ULEM[command], items)
}

// a soul command, colored by the setting current where it stands
pub fn render_soul(el, items, settings, custom_colors) {
    let command = SOUL_ALIASES[string(name(el))] or string(name(el))
    let spec = SOUL[command]
    let raw = if (spec.setting == null) null
        else util.setting_at(settings, spec.setting, el.source_offset) or spec.fallback
    let resolved = if (raw == null) null else color.resolve_color(raw, custom_colors)
    if (raw != null and resolved == null) color.invalid_color(el)
    else decorated(command, if (resolved == null) spec.css
        else util.str_join([for (part in [spec.css, spec.colors ++ ":" ++ resolved] where part != "") part], ";"),
        items)
}

// ulem underlines \emph unless the normalem option or a later \normalem restores
// italics; \ULforem underlines again
pub fn emph_underlined(settings, offset, normalem_option) {
    let switch = util.setting_at(settings, "ulem-emph", offset);
    (if (switch == null) not normalem_option else switch == "ULforem")
}
