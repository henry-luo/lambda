import model: .mod_model
import media: lambda.doc.media
import sound: .mod_sound
import sketches: .mod_sketches

fn label(x, y, size, words, ink = "#7e705b", more = {}) => <text x:x, y:y,
    'font-family':"Georgia, serif", 'font-size':size, fill:ink, *:more, words>
fn screw(x,y) => <g <circle cx:x, cy:y, r:4, fill:"#d2bd8e", stroke:"#8a7854">;
    <path d:"M" ++ string(x-2) ++ " " ++ string(y+2) ++ " l4 -4", stroke:"#8a7854">>
fn ball(x,y,color,r = 10) => <g
    <circle cx:x+1, cy:y+2, r:r+1, fill:"#4c3f2a", opacity:"0.18">;
    <circle cx:x, cy:y, r:r, fill:"url(#marble-" ++ string(color) ++ ")", stroke:model.INKS[color], 'stroke-width':1>;
    <ellipse cx:x-r*.3, cy:y-r*.4, rx:r*.25, ry:r*.15, fill:"#fff", opacity:".75">>
fn definitions() => <defs
    <pattern id:"paper-dots", patternUnits:"userSpaceOnUse", width:30, height:30,
        <circle cx:15, cy:15, r:.7, fill:"#ae9c76", opacity:".35">>;
    <linearGradient id:"brass", x1:0, y1:0, x2:1, y2:1,
        <stop offset:0, 'stop-color':"#f2d99e">; <stop offset:".5", 'stop-color':"#b49455">;
        <stop offset:1, 'stop-color':"#dfc58b">>
    for (i in 0 to 7) <radialGradient id:"marble-" ++ string(i), cx:".3", cy:".25", r:".8",
        <stop offset:0, 'stop-color':"#ffffff">;
        <stop offset:".32", 'stop-color':model.INKS[i]>;
        <stop offset:1, 'stop-color':"#302c32">>>

// Native lettering and leaders stay crisp over the separate, detailed ink artwork.
fn pen(paths) => <g fill:"none", stroke:"#8e704a", 'stroke-width':.85,
    'stroke-linecap':"round", 'stroke-linejoin':"round",
    for (p in paths) <g <path d:p>;
        <path d:p, transform:"translate(.7 -.5)", opacity:".25", 'stroke-width':.55>>>
fn manuscript(x,y,lines,angle = -4,size = 13) => <g
    transform:"translate(" ++ string(x) ++ " " ++ string(y) ++ ") rotate(" ++ string(angle) ++ ")",
    for (i,words in lines) label(0,i*15,size,words,"#876b47",
        {'font-family':"Apple Chancery, Georgia, serif",'font-style':"italic"})>
let MARGIN_NOTES = [
    {x:440,y:234,angle:5,lines:["One passage turns the tongue;", "the next shall find another road."],
        leader:"M433 240 Q407 221 389 227 M394 223 l-5 4 6 1"},
    {x:440,y:224,angle:3,lines:["Each little tongue remembers", "the traveller before it."],
        leader:"M442 237 Q420 236 409 218 M408 224 l1 -6 6 2"},
    {x:282,y:204,angle:-5,lines:["A distant tongue is moved", "by this small connection."],
        leader:"M294 211 Q259 223 226 192 M227 199 l-1 -7 7 2"},
    {x:438,y:214,angle:4,lines:["Let iron pay the passage;", "keep the colors for their vessels."],
        leader:"M441 220 Q410 192 388 181 M393 181 h-5 l2 5"},
    {x:436,y:222,angle:3,lines:["Twice must every vessel receive", "its own particular color."],
        leader:"M443 245 Q469 252 485 274 M479 271 l6 3 -1 -6"},
    {x:427,y:215,angle:4,lines:["A single sphere of night", "may wear whatever color it finds."],
        leader:"M428 222 Q397 225 386 205 M385 211 l1 -6 6 2"},
    {x:296,y:201,angle:-4,lines:["The two ways converse;", "observe what the other has moved."],
        leader:"M307 210 Q286 202 273 181 M273 187 v-6 l6 3"},
    {x:430,y:214,angle:4,lines:["Send iron past a vessel full;", "the deeper branches still hunger."],
        leader:"M428 220 Q404 222 388 214 M394 213 l-6 1 4 4"},
    {x:435,y:231,angle:3,lines:["In three equal turns", "the little wheel returns."],leader:"M431 237 Q409 220 388 220 M394 217 l-6 3 6 1"},
    {x:433,y:212,angle:4,lines:["Two weights persuade the latch;", "a third may pass unhindered."],leader:"M430 218 Q407 184 388 176 M394 175 l-6 1 3 5"},
    {x:410,y:214,angle:4,lines:["The lodestone knows its iron;", "glass shall not be drawn aside."],leader:"M408 219 Q394 202 387 187 M387 193 v-6 l6 3"},
    {x:418,y:222,angle:4,lines:["A blue ray passes to the left;", "all other hues take the right."],leader:"M414 228 Q399 221 386 212 M392 212 h-6 l3 5"},
    {x:295,y:224,angle:-4,lines:["The wound spring lends its force;", "the sphere ascends once more."],leader:"M298 237 Q272 269 208 294 M213 290 l-5 4 6 1"},
    {x:403,y:206,angle:4,lines:["Let iron serve the heavy latch;", "the glass is kept for color."],leader:"M402 212 Q390 186 386 175 M385 181 l1 -6 5 4"},
    {x:279,y:206,angle:-4,lines:["One inlet whispers to the wheel;", "the other follows its reply."],leader:"M290 214 Q256 208 226 183 M227 189 l-1 -6 6 2"},
    {x:336,y:250,angle:-4,lines:["Of weight, of color, and of flight:", "each small device must play its part."],leader:"M342 257 Q316 290 279 299 M284 295 l-5 4 6 1"}
]
fn background_notes(game) {
    let note = MARGIN_NOTES[game.stage];
    <g 'aria-hidden':"true", 'pointer-events':"none",
        <g opacity:".45",
            (manuscript(43,37,["De motu et pondere"],-3,13));
            (manuscript(494,37,["studies for a curious machine"],2,11))>;
        <g opacity:".63", (manuscript(note.x,note.y,note.lines,note.angle)); (pen([note.leader]))>;
        <g opacity:".35",
            (manuscript(29,478,["λ · fol. " ++ string(game.stage+1)],-90,11));
            (pen(["M678 299 v70 M674 299 h8 M674 369 h8 M680 308 l3 3 -3 3 M680 353 l3 3 -3 3"]))>>
}
fn gear(node, bit) {
    let points = if (node.kind == "rotor") [[-12,10],[0,15],[12,10]] else [[-12,10],[12,10]]
    let arm = "M0 -12 L" ++ string(points[bit][0]) ++ " " ++ string(points[bit][1]);
    <g transform:"translate(" ++ string(node.x) ++ " " ++ string(node.y) ++ ")",
    for (i in 0 to 11) <rect x:-3, y:-23, width:6, height:9, rx:1, fill:"#ac8b4e",
        transform:"rotate(" ++ string(i*30) ++ ")">;
    <circle r:18, fill:"url(#brass)", stroke:"#7d653e", 'stroke-width':1.5>;
    <circle r:12, fill:"#e9dab7", stroke:"#a88c58">;
    <path d:arm, stroke:"#625036",
        'stroke-width':6, 'stroke-linecap':"round">;
    <path d:arm, stroke:"#f4d99b",
        'stroke-width':3, 'stroke-linecap':"round">;
    <circle r:4, fill:"#746046">;
    (label(29,5,13,node.label ++ (if (node.kind == "rotor") " · " ++ string(bit+1) ++ "/3" else "")))>
}
fn pocket(node, filled) => <g
    <path d:"M" ++ string(node.x-29) ++ " 417 v32 q29 22 58 0 v-32", fill:"url(#brass)", stroke:"#8b7450", 'stroke-width':2>;
    <ellipse cx:node.x, cy:419, rx:29, ry:10, fill:"#8b7757", stroke:"#d2bc89", 'stroke-width':3>;
    <ellipse cx:node.x, cy:419, rx:21, ry:6, fill:model.INKS[node.color], opacity:".7">;
    if (filled > 0) ball(node.x,429,node.color,12);
    (label(node.x,479,12,upper(node.label),"#75654c",{'text-anchor':"middle",'letter-spacing':1}));
    for (i in 0 to node.need-1) <circle cx:node.x+(i-(node.need-1)/2)*13, cy:494, r:3.5,
        fill:if (i < filled) model.INKS[node.color] else "#d7c8a5", stroke:model.INKS[node.color]>>
fn device(node, bit, filled) {
    if (model.indexed(node)) gear(node,bit)
    else if (node.kind == "bin") pocket(node,filled)
    else if (model.catching(node)) <g
        <circle cx:node.x, cy:node.y, r:21, fill:"#e8d6ac", stroke:"#a0844f", 'stroke-width':3>;
        <path d:"M" ++ string(node.x-17) ++ " " ++ string(node.y) ++ (if (bit < node.threshold) " h34" else " l30 -24"),
            stroke:"#8d6744", 'stroke-width':7, 'stroke-linecap':"round">;
        (if (bit > 0) ball(node.x,node.y+2,6,7));
        (if (node.kind == "counter") <g
            <path d:"M" ++ string(node.x-17) ++ " " ++ string(node.y+26) ++ " h34 l-5 7 h-24 Z", fill:"url(#brass)", stroke:"#8b7450">;
            (label(node.x,node.y+24,10,string(bit) ++ "/" ++ string(node.threshold),"#75654c",{'text-anchor':"middle"}))>);
        (label(node.x+31,node.y+5,11,if (bit < node.threshold) "CLOSED" else "OPEN"))>
    else if (node.kind == "magnet") <g transform:"translate(" ++ string(node.x) ++ " " ++ string(node.y) ++ ")",
        <path d:"M-17 -17 V7 A17 17 0 0 0 17 7 V-17", fill:"none", stroke:"#80613d", 'stroke-width':13>;
        <path d:"M-17 -17 V7 A17 17 0 0 0 17 7 V-17", fill:"none", stroke:"#dbbc7e", 'stroke-width':8>;
        <path d:"M-17 -17 v9 M17 -17 v9", fill:"none", stroke:"#909a9d", 'stroke-width':10>;
        (label(31,4,10,"MAGNET"))>
    else if (node.kind == "filter") <g transform:"translate(" ++ string(node.x) ++ " " ++ string(node.y) ++ ")",
        <path d:"M0 -25 L25 0 0 25 -25 0 Z", fill:"url(#brass)", stroke:"#8b7450", 'stroke-width':2>;
        <circle r:16, fill:"url(#marble-" ++ string(node.match) ++ ")", stroke:"#f8e8bb", 'stroke-width':2>;
        <path d:"M-8 7 L8 -7", stroke:"#fff", 'stroke-width':2, opacity:".6">;
        (label(32,4,10,upper(model.COLORS[node.match]) ++ " LENS"))>
    else if (node.kind == "spring") <g transform:"translate(" ++ string(node.x) ++ " " ++ string(node.y) ++ ")",
        <path d:"M-22 24 H22 M-19 20 V-5 M19 20 V-5", fill:"none", stroke:"#8b7450", 'stroke-width':4>;
        <path d:"M0 21 L-12 16 12 10 -12 4 12 -2 -12 -8 12 -14 0 -20", fill:"none", stroke:"#a68b53", 'stroke-width':3>;
        <path d:"M-18 -20 H18 M0 -25 V-40 M-5 -35 L0 -41 5 -35", fill:"none", stroke:"#80613d", 'stroke-width':3>;
        (label(32,4,10,"SPRING"))>
    else if (node.kind == "relay") <g
        <path d:"M" ++ string(node.x) ++ " " ++ string(node.y-20) ++ " l20 20 -20 20 -20 -20 Z",
            fill:"url(#brass)", stroke:"#8b7450", 'stroke-width':2>;
        (label(node.x,node.y+5,17,"↔","#6b5738",{'text-anchor':"middle"}));
        (label(node.x-45,node.y+5,11,"LINK", "#7e705b",{'text-anchor':"end"}))>
    else <g
        <path d:"M" ++ string(node.x-32) ++ " 29 h64 l-24 30 v14 h-16 v-14 Z",
            fill:"url(#brass)", stroke:"#8c7348", 'stroke-width':2>;
        <ellipse cx:node.x, cy:29, rx:32, ry:8, fill:"#8c7855", stroke:"#d4ba7d", 'stroke-width':2>>
}
fn drawing(game) {
    let machine = model.level(game);
    <svg xmlns:"http://www.w3.org/2000/svg", viewBox:"0 0 720 518", width:720, height:518, role:"img",
        'aria-label':"Marble machine: brass rails, automatic switches and colored collection pockets",
        (definitions());
        <rect width:720, height:510, fill:"url(#paper-dots)">;
        <rect x:10, y:10, width:700, height:498, rx:4, fill:"none", stroke:"#cbb991">;
        (screw(20,20)); (screw(700,20)); (screw(20,498)); (screw(700,498));
        (label(36,59,11,"fig. " ++ string(game.stage+1),"#9b8966",{'font-style':"italic"}));
        (background_notes(game));
        for (node in machine.nodes where node.kind == "relay") <path
            d:"M" ++ string(node.x+22) ++ " " ++ string(node.y) ++ " H" ++ string(machine.nodes[node.link].x) ++
                " V" ++ string(machine.nodes[node.link].y-26),
            fill:"none", stroke:"#b29763", 'stroke-width':1.5, 'stroke-dasharray':"4 5">;
        for (i,node in machine.nodes, j,destination in node.exits) {
            let path = model.rail(node,machine.nodes[destination])
            let live = (not model.indexed(node) and not model.selective(node)) or
                model.outlet(machine,game.bits,i,route_color(game)) == j;
            <g fill:"none", 'stroke-linecap':"round",
                (if (node.kind == "spring") <path d:path, stroke:"#9c7b46", 'stroke-width':2, 'stroke-dasharray':"4 5"> else <g
                    <path d:path, stroke:"#b7a176", 'stroke-width':15, opacity:".35", transform:"translate(1 3)">;
                    <path d:path, stroke:"#8e7956", 'stroke-width':12>;
                    <path d:path, stroke:if (live) "#e8cb8f" else "#b9aa8b", 'stroke-width':9>;
                    <path d:path, stroke:if (live) "#fbefca" else "#c8b998", 'stroke-width':3>>)>
        };
        for (i,node in machine.nodes) device(node,game.bits[i],game.filled[i])
    >
}
// The apparatus changes only at a device or pocket; rolling needs just one small overlay.
pub fn apparatus(game) => media.data_uri(binary(format(drawing(game),'xml')^),"image/svg+xml")^
fn route_color(game) => if (game.active == null) game.selected else game.active.color
pub fn apparatus_key(game) => {stage:game.stage,bits:game.bits,filled:game.filled,
    route_color:if (any([for (node in model.level(game).nodes) model.selective(node)])) route_color(game) else -1}
pub fn rolling_transform(game) {
    let pos = model.ball_position(game)
    // transform percentages use the marble's own 20 by 20 logical-pixel box
    "translate(" ++ string((pos.x-10)*5) ++ "%," ++ string((pos.y-10)*5) ++ "%)"
}
fn rolling(game) {
    let active = game.active;
    <div id:"rolling-marble", class:"rolling-marble" ++ (if (model.level(game).nodes[active.from].kind == "spring") " airborne" else ""),
        'aria-hidden':"true", style:"transform:" ++ rolling_transform(game) ++
        ";background:radial-gradient(circle at 30% 25%,#fff," ++
        model.INKS[active.color] ++ " 42%,#353139 100%)">
}
fn button(id, command, caption, blocked = false, value = 0) => <button id:id, type:"button",
    'data-action':command, 'data-value':string(value), *:(if (blocked) {disabled:""} else {}), caption>
fn device_caption(node,bit) {
    if (node.kind == "rotor") "Distributor " ++ node.label ++ " · next " ++ string(bit+1) ++ "/3"
    else if (node.kind == "counter") "Counterweight · " ++ string(bit) ++ "/" ++ string(node.threshold) ++ " loaded"
    else if (node.kind == "magnet") "Magnet · steel left, glass right"
    else if (node.kind == "filter") model.COLORS[node.match] ++ " lens · matching left, others right"
    else "Spring · launches upward"
}
fn device_guide(game) => <div class:"device-guide", 'aria-label':"Machine devices",
    for (i,node in model.level(game).nodes where contains(["rotor","counter","magnet","filter","spring"],node.kind))
        <span 'data-device-kind':node.kind, 'data-node':string(i), 'data-setting':string(game.bits[i]),
            'data-route':string(model.outlet(model.level(game),game.bits,i,route_color(game))),device_caption(node,game.bits[i])>>
fn tray(game) => <section class:"tray", 'aria-label':"Marble tray",
    <div class:"tray-heading", <span class:"eyebrow", "YOUR MARBLES"> <span "keys 1–8">>
    <div class:"marbles",
        for (i in 0 to 7) <button id:"color-" ++ string(i), type:"button",
            class:"marble-choice" ++ (if (game.selected == i) " selected" else ""),
            'data-action':"select", 'data-value':string(i), 'aria-label':model.COLORS[i] ++ " marble",
            'aria-pressed':string(game.selected == i),
            *:(if (game.stock[i] == 0 or game.autoplay) {disabled:""} else {}),
            <span class:"swatch", style:"background:radial-gradient(circle at 30% 25%,#fff," ++ model.INKS[i] ++ " 42%,#353139 100%)">
            <span class:"marble-name", model.COLORS[i]>
            <span class:"quantity", "×" ++ string(game.stock[i])>>>
    <p class:"tray-note", "Steel tests a route. Black matches any color.">>
fn notebook(game) => <aside class:"notebook",
    <p class:"eyebrow", "THE INVENTOR’S NOTEBOOK">;
    <h2 model.level(game).title>;
    <div class:"rule-line">;
    <p class:"lesson", model.level(game).note>;
    <div class:"instructions", <p <b "01">; " Choose a color below the machine.">;
        <p <b "02">; " Drop it into inlet A or B.">;
        <p <b "03">; " Fill every matching pocket.">>;
    <div class:"scorecard",
        <div <span "COLLECTED"> <strong id:"collected", string(model.collected(game)) ++ " / " ++ string(model.required(game))>>;
        <div <span "DROPS / PAR"> <strong id:"moves", string(game.moves) ++ " / " ++ string(model.level(game).par)>>>;
    <div id:"status", class:"status" ++ (if (game.mode == "won") " success" else ""), role:"status", 'aria-live':"polite",
        (if (game.paused) "Paused. Your machine will wait." else game.message)>;
    (if (game.mode == "won") <div class:"solved",
        <div class:"stars", join([for (i in 1 to game.medals[game.stage]) "★"],"")>;
        (button("next","next",if (game.stage == len(model.LEVELS)-1) "Play again  →" else "Next invention  →"))>);
    <div class:"tools", (button("undo","undo","↶ Undo",len(game.history) == 0));
        (button("restart","restart","↻ Restart"));
        (button("hint","hint","A little hint",game.active != null or game.mode == "won"));
        (button("auto-play","auto",if (game.autoplay) "Stop Auto Play" else "Auto Play",game.mode == "won" or (game.active != null and not game.autoplay)))>;
    (if (game.hint != "") <p id:"hint-text", class:"hint-text", game.hint>);
    <p class:"margin-note", "Think one marble ahead.">>

pub fn page(game, apparatus, audio, composition) => <main id:"marble-drop", class:"page", 'data-mode':game.mode,
    'data-level':string(game.stage+1), 'data-active':string(game.active != null),
    'data-paused':string(game.paused), 'data-autoplay':string(game.autoplay),
    'data-collected':string(model.collected(game)), 'data-moves':string(game.moves), 'data-losses':string(game.losses),
    'data-sound':sound.status(audio), 'data-sound-count':string(audio.played), 'data-last-sound':audio.last,
    'data-sound-voices':string(len(audio.voices)),
    <header class:"masthead",
        <div class:"brand", <span class:"brand-icon", "λ">;
            <div <p class:"eyebrow", "THE MECHANICAL PUZZLE COLLECTION">
                <h1 "Marble Drop" <span ".">>>>;
        <span class:"edition", string(len(model.LEVELS)) ++ " LITTLE INVENTIONS">>;
    <nav class:"levels", 'aria-label':"Choose a puzzle",
        <span class:"eyebrow", "INVENTIONS">;
        (for (i in 0 to len(model.LEVELS)-1) <button id:"level-" ++ string(i+1), type:"button",
            class:"level" ++ (if (game.stage == i) " current" else "") ++ (if ((game.medals[i] or 0) > 0) " completed" else ""),
            'data-action':"level", 'data-value':string(i), title:model.LEVELS[i].title,
            'aria-label':"Puzzle " ++ string(i+1) ++ ": " ++ model.LEVELS[i].title,
            'aria-current':if (game.stage == i) "step" else "false", string(i+1)>);
        <span class:"level-caption", "NO. " ++ string(game.stage+1) ++ " / " ++ string(len(model.LEVELS))>>;
    <div class:"workbench",
        <section class:"machine-panel",
            <div class:"machine-top", <span "THE APPARATUS"> <span "BRASS · GLASS · A LITTLE GRAVITY">>;
            <div class:"drawing", (sketches.underlay(game.stage,composition));
                <img class:"machine-svg", src:apparatus,
                alt:"Marble machine with brass rails, automatic switches and colored collection pockets", width:720, height:518>;
                (if (game.active != null) rolling(game));
                for (i,index in model.level(game).funnels) {
                    let node = model.level(game).nodes[index];
                    <button id:"inlet-" ++ node.label, type:"button", class:"inlet", 'data-action':"inlet", 'data-value':string(i),
                        style:"left:" ++ string(node.x/720*100) ++ "%", title:"Drop selected marble into inlet " ++ node.label,
                        *:(if (game.active != null or game.paused or game.autoplay or game.mode == "won") {disabled:""} else {}),
                        "DROP " ++ node.label ++ " ↓">
                };
                (if (game.paused) <div class:"pause-label", "PAUSED">)>;
            <div class:"machine-bottom", <span "Levers flip automatically as marbles pass.">;
                <div class:"machine-controls",
                    <button id:"sound", type:"button", 'data-action':"sound", 'aria-pressed':string(audio.enabled),
                        title:"Toggle sound effects [M]", (if (audio.failed) "Sound unavailable" else if (audio.enabled) "♪ Sound on" else "♪ Sound off")>;
                    (button("pause","pause",if (game.paused) "Resume [P]" else "Pause [P]",game.mode == "won"))>>;
            (device_guide(game));
            (tray(game));
            <div class:"drop-row", <span "Selected: " ++ model.COLORS[game.selected]>;
                (if (len(model.level(game).funnels) > 1) <div class:"inlet-picker",
                    for (i,index in model.level(game).funnels) <button 'data-action':"funnel", 'data-value':string(i),
                        class:if (game.funnel == i) "chosen" else "", "Inlet " ++ model.level(game).nodes[index].label>>);
                (button("drop","drop","Drop marble  ↓",game.active != null or game.paused or game.autoplay or game.mode == "won" or game.stock[game.selected] == 0))>>;
        (notebook(game))>
    <footer <span "1–8 color · A / B drop · Space drop · U undo · H hint · P pause · M sound · R restart">
        <span "MADE OF LAMBDA, NOT MAGIC.">>>
