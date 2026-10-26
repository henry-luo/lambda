// Page presentation for the reader: paper textures, the antique frame and its
// corner flourishes, and page-turn animations. Paper is host-side layers under
// a transparent chapter frame, so it stays put while columns scroll past.
import media: lambda.doc.media
import store: .state

fn asset(name) => media.data_uri(input(sys.lambda.home# ++ "/package/doc/ebook/textures/" ++ name,"binary")^,"image/png") ^ { "" }
// seamless alpha noise from utils/generate_ebook_textures.py
let grain = "url(" ++ asset("grain.png") ++ ")"
let mottle = "url(" ++ asset("mottle.png") ++ ")"

// sheet: the paper color layers; grain and mottle: tile sizes of the noise
// layers (0 hides one); ink: chapter text; ornament: the antique frame
let papers = {
    antique:{ink:"#3a2a1a",base:"#efe2c4",ornament:"#7a5532",rule:"#a8835a",grain:256,mottle:720,
        sheet:"radial-gradient(ellipse at 50% 45%,#f5ebd2 30%,#ead7ac 78%,#d6ba83 100%)"},
    parchment:{ink:"#402d1c",base:"#f1e3c4",grain:256,mottle:520,sheet:"radial-gradient(ellipse at 50% 50%,#f8edd4 40%,#ebd7aa 100%)"},
    linen:{ink:"#2e2a25",base:"#f4efe4",grain:384,mottle:0,
        sheet:"repeating-linear-gradient(0deg,#6e5f460b 0 1px,transparent 1px 3px),repeating-linear-gradient(90deg,#6e5f4608 0 1px,transparent 1px 4px),linear-gradient(#f7f3ea,#f0eadc)"},
    newsprint:{ink:"#242424",base:"#e7e5de",grain:192,mottle:0,sheet:"linear-gradient(#edebe5,#e2e0d8)"},
    ivory:{ink:"#2b2620",base:"#fbf7ec",grain:640,mottle:0,sheet:"linear-gradient(#fefbf3,#f6f0e1)"}}

pub fn ink(texture) => if (papers[texture] == null) null else papers[texture].ink
// the antique frame needs a margin clear of its rules and flourishes
pub fn margin(prefs) => if (prefs.texture == "antique" and prefs.mode != "scroll") max(prefs.margin,76) else prefs.margin
pub fn animated(prefs) => prefs.mode != "scroll" and prefs.turn != "none"

// One flourish drawn for the top-left corner; the others mirror it.
let flourish = "<path d='M16 16 C16 44 44 58 58 44 C70 32 58 14 44 22 C34 28 38 42 48 40' fill='none' stroke-width='3.2' stroke-linecap='round'/>" ++
    "<path d='M16 16 C46 12 78 14 108 16 C118 17 120 26 112 29 C106 31 103 25 108 23' fill='none' stroke-width='2' stroke-linecap='round'/>" ++
    "<path d='M16 16 C12 46 14 78 16 108 C17 118 26 120 29 112 C31 106 25 103 23 108' fill='none' stroke-width='2' stroke-linecap='round'/>" ++
    "<path d='M30 20 C42 8 62 8 70 18 C58 18 44 22 30 20 Z' stroke='none'/><path d='M20 30 C8 42 8 62 18 70 C18 58 22 44 20 30 Z' stroke='none'/>" ++
    "<path d='M58 44 C72 50 78 68 66 76 C57 82 48 72 56 66' fill='none' stroke-width='2.4' stroke-linecap='round'/>" ++
    "<path d='M44 58 C50 72 68 78 76 66' fill='none' stroke-width='2' stroke-linecap='round'/>" ++
    "<path d='M78 22 C84 14 94 14 96 20 C90 20 84 22 78 22 Z' stroke='none'/><path d='M22 78 C14 84 14 94 20 96 C20 90 22 84 22 78 Z' stroke='none'/>" ++
    "<path d='M16 5 L27 16 L16 27 L5 16 Z' stroke='none'/>"
let mirrors = {tl:"",tr:"translate(124 0) scale(-1 1)",bl:"translate(0 124) scale(1 -1)",br:"translate(124 124) scale(-1 -1)"}
fn corner(color,key) => media.data_uri(binary("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 124 124'><g fill='" ++ color ++
    "' stroke='" ++ color ++ "' transform='" ++ mirrors[key] ++ "'>" ++ flourish ++ "</g></svg>"),"image/svg+xml") ^ { "" }

// The back of a turning leaf: blank paper for a turn, landing on the facing
// page of a spread; a flip shows the new facing page (`next`) on it there.
pub fn back(next) => <div class:"ebook-turn-back",*noise();
    <div class:"ebook-turn-back-content",(paper()); next>; <div class:"ebook-flip-shade">>
// A flip keeps the old facing page (`old`) under its leaf and casts the leaf's
// shadow on the page it uncovers; flip.ls places them.
pub fn hold(old) => [<div class:"ebook-turn-hold",(paper()); old>, <div class:"ebook-flip-cast",<div class:"ebook-flip-shade">>]
// the page corners a flip can be dragged by
let corner_keys = ["top-left","top-right","bottom-left","bottom-right"]
pub fn corners() => [for (key in corner_keys) <div class:"ebook-flip-corner",["data-corner"]:key,["aria-hidden"]:"true">]
// Both sheets are always present; the right one shows only in a spread.
fn noise() => [<div class:"ebook-mottle">,<div class:"ebook-grain">]
pub fn paper() => <div class:"ebook-paper",
    for (side in ["left","right"]) <div class:"ebook-sheet",["data-side"]:side,
        *noise();
        <div class:"ebook-rule">; <div class:"ebook-rule ebook-rule-inner">
        for (key in ["tl","tr","bl","br"]) <div class:"ebook-corner",["data-corner"]:key>>;
    <div class:"ebook-gutter">>

fn texture_css(id) => (let p = papers[id], let at = ".ebook-stage[data-texture=" ++ id ++ "] ",
    at ++ ".ebook-sheet," ++ at ++ ".ebook-turn-back{background:" ++ p.sheet ++ "," ++ p.base ++ "}" ++
    at ++ ".ebook-grain{display:block;background-size:" ++ string(p.grain) ++ "px}" ++
    (if (p.mottle == 0) "" else at ++ ".ebook-mottle{display:block;background-size:" ++ string(p.mottle) ++ "px}") ++
    at ++ ".ebook-frame," ++ at ++ ".ebook-copy{background:transparent}" ++
    (if (p.ornament == null) "" else
        at ++ "{background:linear-gradient(#5d3b22,#3e2516)}" ++
        at ++ ".ebook-page," ++ at ++ ".ebook-turn{inset:10px}" ++ at ++ ".ebook-sheet{box-shadow:inset 0 0 18px #6b4a2a40}" ++
        ".ebook-stage[data-texture=" ++ id ++ "]:not([data-layout=scroll]) .ebook-rule{display:block;border-color:" ++ p.rule ++ "}" ++
        ".ebook-stage[data-texture=" ++ id ++ "]:not([data-layout=scroll]) .ebook-corner{display:block}" ++
        join([for (key in ["tl","tr","bl","br"]) at ++ ".ebook-corner[data-corner=" ++ key ++ "]{background-image:url(" ++ corner(p.ornament,key) ++ ")}"],"")))

// Turn keyframes per kind and direction; `layer` moves the old page (or only
// keeps it shown), `page` brings in the new one, and `leaf`, `back` and
// `shade` fold the old page over the spine.
fn frames(kind,dir) => (let s = if (dir == "next") "-" else "", let r = if (dir == "next") "" else "-",
    if (kind == "slide-left") {layer:"from{opacity:1;transform:translateX(0)}to{opacity:1;transform:translateX(" ++ s ++ "100%)}",
        page:"from{transform:translateX(" ++ r ++ "100%)}to{transform:translateX(0)}"}
    else if (kind == "slide-up") {layer:"from{opacity:1;transform:translateY(0)}to{opacity:1;transform:translateY(" ++ s ++ "100%)}",
        page:"from{transform:translateY(" ++ r ++ "100%)}to{transform:translateY(0)}"}
    else if (kind == "dissolve") {layer:"from{opacity:1}to{opacity:0}"}
    // A turning leaf folds toward the spine (2D: scaleX with a slight skew for lift);
    // in a spread the blank back of the leaf then unfolds over the facing page.
    else if (kind == "turn") {layer:"0%,99%{opacity:1}100%{opacity:0}",
        leaf:"0%{transform:scaleX(1) skewY(0deg)}60%{transform:scaleX(.45) skewY(" ++ s ++ "3deg)}100%{transform:scaleX(0) skewY(" ++ s ++ "6deg)}",
        shade:"from{opacity:0}to{opacity:.5}"}
    else {layer:"0%,99%{opacity:1}100%{opacity:0}",
        leaf:"0%{transform:scaleX(1) skewY(0deg)}50%{transform:scaleX(0) skewY(" ++ s ++ "5deg)}100%{transform:scaleX(0)}",
        back:"0%,50%{opacity:1;transform:scaleX(0) skewY(" ++ r ++ "5deg)}85%{opacity:1;transform:scaleX(1) skewY(0deg)}100%{opacity:0;transform:scaleX(1)}",
        shade:"0%{opacity:0}50%{opacity:.5}100%{opacity:.5}"})
let durations = {["slide-left"]:".45s",["slide-up"]:".45s",dissolve:".5s",turn:".6s",spread:".7s"}
let targets = {layer:".ebook-turn",page:".ebook-page",leaf:".ebook-turn-page",back:".ebook-turn-back",shade:".ebook-turn-shade"}
// Two parities per name: setting the other parity restarts the animation.
fn turn_css(kind,dir,parity) => (let key = kind ++ "-" ++ dir ++ "-" ++ string(parity),
    let spread = kind == "turn", let parts = frames(kind,dir), let wide = frames("spread",dir),
    let select = (layout) => ".ebook-stage" ++ layout ++ "[data-turn=\"" ++ key ++ "\"] ",
    join([for (part,body in parts) "@keyframes ebook-" ++ key ++ "-" ++ string(part) ++ "{" ++ body ++ "}" ++
        select("") ++ targets[part] ++ "{animation:ebook-" ++ key ++ "-" ++ string(part) ++ " " ++ durations[kind] ++ " ease-in-out forwards}"],"") ++
    (if (not spread) "" else join([for (part,body in wide) "@keyframes ebook-s" ++ key ++ "-" ++ string(part) ++ "{" ++ body ++ "}" ++
        select("[data-layout=spread]") ++ targets[part] ++ "{animation:ebook-s" ++ key ++ "-" ++ string(part) ++ " " ++ durations.spread ++ " ease-in-out forwards}"],"")))
// a flip being turned or a corner curled by the pointer (a peel)
fn turning(rule) => ".ebook-stage[data-turn=flip]" ++ rule ++ ",.ebook-stage[data-turn=peel]" ++ rule
pub fn turn_name(kind,dir,parity) => kind ++ "-" ++ dir ++ "-" ++ string(parity)

pub let stylesheet = "
.ebook-stage{position:relative;flex:1;min-height:120px;overflow:hidden;background:var(--ebook-panel)}
.ebook-page,.ebook-turn{position:absolute;inset:0}
.ebook-page .ebook-frame{position:absolute;left:0;top:0;width:100%;height:100%}
.ebook-stage:not([data-layout=scroll]) .ebook-frame,.ebook-copy{overflow:hidden}
.ebook-paper{position:absolute;inset:0;display:flex}.ebook-sheet{position:relative;flex:1}.ebook-sheet[data-side=right]{display:none}
.ebook-stage[data-layout=spread] .ebook-sheet[data-side=right]{display:block}
.ebook-gutter{display:none;position:absolute;top:0;bottom:0;left:calc(50% - 30px);width:60px;background:linear-gradient(to right,#0000,#00000012 35%,#0000003d 50%,#00000012 65%,#0000)}
.ebook-stage[data-layout=spread] .ebook-gutter{display:block}
.ebook-grain,.ebook-mottle{display:none;position:absolute;inset:0;background-repeat:repeat}
.ebook-grain{background-image:" ++ grain ++ "}.ebook-mottle{background-image:" ++ mottle ++ "}
.ebook-rule,.ebook-corner{display:none;position:absolute}.ebook-rule{inset:22px;border:1px solid}.ebook-rule-inner{inset:26px}
.ebook-corner{width:78px;height:78px;background-size:contain;background-repeat:no-repeat}
.ebook-corner[data-corner=tl]{left:10px;top:10px}.ebook-corner[data-corner=tr]{right:10px;top:10px}.ebook-corner[data-corner=bl]{left:10px;bottom:10px}.ebook-corner[data-corner=br]{right:10px;bottom:10px}
.ebook-turn{z-index:2;opacity:0;pointer-events:none}.ebook-stage[data-turn=hold] .ebook-turn{opacity:1}
.ebook-turn-page{position:absolute;inset:0;overflow:hidden;transform-origin:0 50%}
.ebook-turn-content{position:absolute;left:0;top:0;width:100%;height:100%}
.ebook-copy{position:absolute;left:0;top:0;width:100%;height:100%;border:0}
.ebook-turn-shade{position:absolute;inset:0;opacity:0;background:linear-gradient(to left,#0000,#0000005c)}
.ebook-turn-back{display:none;position:absolute;top:0;bottom:0;width:50%;background:var(--ebook-panel)}
.ebook-stage[data-turn-kind=turn-prev] .ebook-turn-page{transform-origin:100% 50%}
.ebook-stage[data-turn-kind=turn-prev] .ebook-turn-shade{background:linear-gradient(to right,#0000,#0000005c)}
.ebook-stage[data-layout=spread][data-turn-kind=turn-next] .ebook-turn-page{left:50%;transform-origin:0 50%}
.ebook-stage[data-layout=spread][data-turn-kind=turn-next] .ebook-turn-content{left:-100%;width:200%}
.ebook-stage[data-layout=spread][data-turn-kind=turn-prev] .ebook-turn-page{right:50%;transform-origin:100% 50%}
.ebook-stage[data-layout=spread][data-turn-kind=turn-prev] .ebook-turn-content{width:200%}
.ebook-stage[data-layout=spread][data-turn-kind=turn-next] .ebook-turn-back{display:block;left:0;transform-origin:100% 50%}
.ebook-stage[data-layout=spread][data-turn-kind=turn-prev] .ebook-turn-back{display:block;left:50%;transform-origin:0 50%}
.ebook-turn-hold,.ebook-turn-back-content,.ebook-flip-cast{display:none;position:absolute;inset:0}
.ebook-flip-shade{position:absolute;left:0;top:0;width:0;height:4000px;transform-origin:0 0}
" ++ turning(" .ebook-turn") ++ "{opacity:1}" ++ turning(" .ebook-flip-cast") ++ "{display:block}
" ++ turning(" .ebook-turn-back") ++ "{display:block;left:0;width:100%;transform-origin:0 0}
.ebook-flip-corner{display:none;position:absolute;z-index:4;width:64px;height:64px;cursor:pointer}.ebook-flip-corner[data-drag=true]{inset:0;width:auto;height:auto;z-index:5}
.ebook-stage[data-flip=true][data-layout=paged] .ebook-flip-corner,.ebook-stage[data-flip=true][data-layout=spread] .ebook-flip-corner{display:block}
" ++ join([for (key in corner_keys) ".ebook-flip-corner[data-corner=" ++ key ++ "]{" ++ replace(key,"-",":0;") ++ ":0}"],"") ++ "
.ebook-stage[data-turn=flip][data-layout=spread] .ebook-turn-back-content,.ebook-stage[data-turn=flip][data-layout=spread] .ebook-turn-hold{display:block}
.ebook-stage[data-turn-kind=flip-next] .ebook-turn-hold{clip-path:inset(0 50% 0 0)}.ebook-stage[data-turn-kind=flip-prev] .ebook-turn-hold{clip-path:inset(0 0 0 50%)}
.ebook-turn-back .ebook-flip-shade{background:linear-gradient(to right,#00000038,#0000000d 45%,#0000)}
.ebook-flip-cast .ebook-flip-shade{background:linear-gradient(to right,#00000061,#00000014 50%,#0000)}
" ++ join([for (id in store.textures where id != "none") texture_css(id)],"") ++
    join([for (kind in store.turns,dir in ["next","prev"],parity in [0,1] where kind != "none" and kind != "flip") turn_css(kind,dir,parity)],"")
