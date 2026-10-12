// semantic tokens keep layout independent of skin and host.
import c: .common

let neutral = {background:"#ffffff", text:"#202833", muted:"#64748b", border:"#dbe1e8", primary:"#2563eb",
    selected:"#dbeafe", card:"#f8fafc", radius:8, font_family:"Arial, sans-serif", font_size:14}
let dark = {background:"#111827", text:"#f3f4f6", muted:"#a7b5c7", border:"#475569", primary:"#60a5fa", selected:"#1e3a5f", card:"#1f2937"}
let contrast = {background:"#ffffff", text:"#000000", muted:"#222222", border:"#000000", primary:"#0000cc", selected:"#ffff00", card:"#ffffff"}

pub fn tokens(spec, snapshot = null) {
    let authored = if (snapshot.theme != null) snapshot.theme else spec.theme;
    let preset = if (authored is map) authored.preset else authored;
    {*:neutral, *:(if (c.text(preset)=="dark") dark else if (c.text(preset)=="high_contrast") contrast else {}),
        *:c.attrs(authored)}
}
pub fn prefix(spec) string | error => c.text(c.option(c.attrs(spec.theme), "class_prefix", "lambda-timeline"))
pub fn variables(spec, snapshot = null) {
    let t = tokens(spec, snapshot);
    let p = prefix(spec);
    join([for (key in ["background", "text", "muted", "border", "primary", "selected", "card"])
        "--" ++ p ++ "-" ++ key ++ ":" ++ c.text(t[key]) ++ ";"], "") ++
        "--" ++ p ++ "-gap:" ++ c.text(spec.gap) ++ "px;" ++
        "font-family:" ++ c.text(t.font_family) ++ ";font-size:" ++ c.text(t.font_size) ++ "px;" ++
        "background:" ++ c.text(t.background) ++ ";color:" ++ c.text(t.text) ++ ";"
}
pub fn part(spec, key, base = {}) {
    let recipe = spec.parts[key];
    {*:base, *:c.attrs(recipe.attributes), class:c.text(base.class) ++ (if (recipe.class != null) " " ++ c.text(recipe.class) else ""),
        style:c.text(base.style) ++ c.text(recipe.style)}
}
pub fn render(spec, key, model, fallback) => if (spec.parts[key].render is fn) spec.parts[key].render(model,fallback) else fallback

pub fn card_style(spec, item, slot = "body", positioned = false) {
    let cards=spec.cards;
    let alignment=c.option(cards,slot ++ "_align",item.align);
    (if (alignment!=null) "text-align:" ++ c.text(alignment) ++ ";" else "") ++
    (if (slot=="body")
        join([for (key in ["width","height","min_width","max_width","min_height","max_height"] where cards[key]!=null and
            (not positioned or starts_with(key,"min_") or starts_with(key,"max_")))
            replace(key,"_","-") ++ ":" ++ c.text(cards[key]) ++ "px;"],"") ++
        (if (cards.borderless==true) "border:0;" else if (cards.borderless==false) "border:1px solid var(--" ++ prefix(spec) ++ "-border);" else "") ++
        (if (cards.scrollbars==false) "scrollbar-width:none;" else "") ++
        (if (cards.text_overlay==true) (if (positioned) "" else "position:relative;") ++ "isolation:isolate;" else "") else "")
}

// structure is shared by neutral and dtna skins; colors/typography remain tokens.
pub let cards_css = "
.lambda-timeline{list-style:none;padding:0;margin:0;color:var(--lambda-timeline-text)}.lambda-timeline-item{position:relative;display:grid;grid-template-columns:24px minmax(0,1fr);padding:0;min-height:44px}
.lambda-timeline-rail{position:relative;grid-column:1;grid-row:1;display:flex;justify-content:center;color:var(--lambda-timeline-color,var(--lambda-timeline-primary))}
.lambda-timeline-rail:after{content:'';position:absolute;top:20px;bottom:0;inset-inline-start:11px;border-inline-start:2px solid var(--lambda-timeline-border)}
.lambda-timeline-item:last-child .lambda-timeline-rail:after{display:none}.lambda-timeline-rail-pending .lambda-timeline-rail:after{border-inline-start-style:dotted}
.lambda-timeline-marker{display:inline-flex;align-items:center;justify-content:center;position:relative;z-index:1;width:24px;height:24px;background:var(--lambda-timeline-background)}
.lambda-timeline-dot{position:static;width:10px;height:10px;border:2px solid currentColor;border-radius:50%;background:var(--lambda-timeline-background)}
.lambda-timeline-marker-square .lambda-timeline-dot{border-radius:0}.lambda-timeline-marker-diamond .lambda-timeline-dot{border-radius:0;transform:rotate(45deg)}
.lambda-timeline-content{grid-column:2;grid-row:1;min-width:0;padding:0 0 var(--lambda-timeline-gap,20px) 8px}.lambda-timeline-title{font-weight:400;margin-bottom:4px}
.lambda-timeline-labelled .lambda-timeline-item{grid-template-columns:minmax(0,1fr) 24px minmax(0,1fr)}
.lambda-timeline-labelled .lambda-timeline-rail{grid-column:2}.lambda-timeline-labelled .lambda-timeline-title{grid-column:1;grid-row:1;text-align:end;padding-inline-end:8px}
.lambda-timeline-labelled .lambda-timeline-content{grid-column:3;padding-inline-start:8px}
.lambda-timeline-labelled .lambda-timeline-item-end .lambda-timeline-title{grid-column:3;text-align:start;padding-inline-start:8px;padding-inline-end:0}
.lambda-timeline-labelled .lambda-timeline-item-end .lambda-timeline-content{grid-column:1;text-align:end;padding-inline-end:8px;padding-inline-start:0}
.lambda-timeline-end:not(.lambda-timeline-labelled) .lambda-timeline-item{grid-template-columns:minmax(0,1fr) 24px}.lambda-timeline-end:not(.lambda-timeline-labelled) .lambda-timeline-rail{grid-column:2}
.lambda-timeline-end:not(.lambda-timeline-labelled) .lambda-timeline-content{grid-column:1;text-align:end;padding-inline-end:8px}
.lambda-timeline-horizontal{display:flex}.lambda-timeline-horizontal .lambda-timeline-item{flex:1;min-width:0;min-height:0;grid-template-columns:minmax(0,1fr);grid-template-rows:10px auto}
.lambda-timeline-horizontal .lambda-timeline-rail{grid-column:1;grid-row:1;justify-content:center}.lambda-timeline-horizontal .lambda-timeline-marker{height:10px}
.lambda-timeline-horizontal .lambda-timeline-rail:after{inset-inline-start:calc(50% + 10px);inset-inline-end:calc(-50% + 10px);top:4px;bottom:auto;border:0;border-top:2px solid var(--lambda-timeline-border)}
.lambda-timeline-horizontal .lambda-timeline-content{grid-column:1;grid-row:2;padding:8px 0 0;text-align:center;justify-self:center;box-sizing:border-box;max-width:100%}
.lambda-timeline-horizontal.lambda-timeline-labelled .lambda-timeline-item{grid-template-rows:auto 24px auto}
.lambda-timeline-horizontal.lambda-timeline-labelled .lambda-timeline-title{grid-column:1;grid-row:1;text-align:start;padding:0 16px 8px 0}
.lambda-timeline-horizontal.lambda-timeline-labelled .lambda-timeline-rail{grid-row:2}.lambda-timeline-horizontal.lambda-timeline-labelled .lambda-timeline-content{grid-row:3}
.lambda-timeline-horizontal.lambda-timeline-labelled .lambda-timeline-item-end .lambda-timeline-title{grid-column:1;grid-row:3}.lambda-timeline-horizontal.lambda-timeline-labelled .lambda-timeline-item-end .lambda-timeline-content{grid-column:1;grid-row:1}
.lambda-timeline-text-overlay{background:#172033;color:white;overflow:hidden;min-height:160px}.lambda-timeline-text-overlay .lambda-timeline-media{position:absolute;inset:0;z-index:-1;opacity:.45}.lambda-timeline-text-overlay .lambda-timeline-media img{width:100%;height:100%;object-fit:cover}.lambda-timeline-text-overlay .lambda-timeline-card-title{position:relative}.lambda-timeline-text-overlay a{color:inherit}
.lambda-timeline-toolbar{display:flex;flex-wrap:wrap;align-items:center;gap:6px;padding:8px 0;background:var(--lambda-timeline-background);color:var(--lambda-timeline-text)}.lambda-timeline-toolbar button,.lambda-timeline-toolbar select,.lambda-timeline-toolbar input{font:inherit;background:var(--lambda-timeline-card);color:inherit;border:1px solid var(--lambda-timeline-border);border-radius:4px;padding:4px 8px}.lambda-timeline-toolbar button{display:inline-flex;align-items:center;gap:4px}.lambda-timeline-toolbar button:focus-visible,.lambda-timeline-item:focus-visible,.lambda-timeline-overlay-card:focus-visible{outline:2px solid var(--lambda-timeline-primary);outline-offset:2px}.lambda-timeline-toolbar button:disabled{opacity:.5}
"
