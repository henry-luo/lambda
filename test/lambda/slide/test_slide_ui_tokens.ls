// Player chrome theming: `ui` tokens from the deck and options become --slide-ui-* custom properties.
import slide: lambda.slide
import theme: lambda.slide.theme

let deck = <presentation id: "d", ui: {accent: "#ff9500", radius: "4px"}, <slide <text "One">>>
let plain = <presentation <slide <text "One">>>
fn body_style(doc) => [for (c in content(doc) where c is element and name(c) == 'body') c.style][0];
{
    tokens: len(theme.ui_tokens),
    style: theme.ui_style({accent: "#0a84ff", control_border: "rgba(255,255,255,0.1)", font: "-apple-system, sans-serif"})^,
    none: theme.ui_style(null)^,
    deck_only: slide.compile(deck)^.ui,
    merged: slide.compile(deck, {ui: {accent: "#0a84ff", text: "#f2f2f7"}})^.ui,
    plain: slide.compile(plain)^.ui,
    // the option's accent replaces the deck's; the deck's radius stays
    player_style: slide.player(deck, {instance: "p", width: 640.0, height: 360.0, ui: {accent: "#0a84ff"}})^.style,
    page_body: body_style(slide.page(deck)^),
    unknown: (slide.compile(plain, {ui: {accnt: "#000"}}) ^ { ^.message }),
    injection: (slide.compile(plain, {ui: {accent: "red; color: blue"}}) ^ { ^.message }),
    empty: (slide.compile(plain, {ui: {font: ""}}) ^ { ^.message }),
    not_map: (slide.compile(<presentation ui: "dark", <slide <text "x">>>) ^ { ^.message })
}
