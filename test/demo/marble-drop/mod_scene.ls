import model: .mod_model
import media: lambda.doc.media
import sound: .mod_sound

fn label(x, y, size, words, ink = "#7e705b", more = {}) => <text x:x, y:y,
    'font-family':"Georgia, serif", 'font-size':size, fill:ink, *:more, words>
fn screw(x,y) => <g <circle cx:x, cy:y, r:4, fill:"#d2bd8e", stroke:"#8a7854">;
    <path d:"M" ++ string(x-2) ++ " " ++ string(y+2) ++ " l4 -4", stroke:"#8a7854">>
fn ball(x,y,color,r = 10) => <g
    <circle cx:x+1, cy:y+2, r:r+1, fill:"#4c3f2a", opacity:"0.18">;
    <circle cx:x, cy:y, r:r, fill:"url(#marble-" ++ string(color) ++ ")", stroke:model.INKS[color], 'stroke-width':1>;
    <ellipse cx:x-r*.3, cy:y-r*.4, rx:r*.25, ry:r*.15, fill:"#fff", opacity:".75">>
fn definitions() => <defs
    <linearGradient id:"paper", x1:0, y1:0, x2:1, y2:1,
        <stop offset:0, 'stop-color':"#f8f0d8">; <stop offset:1, 'stop-color':"#e9dab7">>
    <linearGradient id:"brass", x1:0, y1:0, x2:1, y2:1,
        <stop offset:0, 'stop-color':"#f2d99e">; <stop offset:".5", 'stop-color':"#b49455">;
        <stop offset:1, 'stop-color':"#dfc58b">>
    for (i in 0 to 7) <radialGradient id:"marble-" ++ string(i), cx:".3", cy:".25", r:".8",
        <stop offset:0, 'stop-color':"#ffffff">;
        <stop offset:".32", 'stop-color':model.INKS[i]>;
        <stop offset:1, 'stop-color':"#302c32">>>

// Original pen studies sit beneath the apparatus; a second fine stroke suggests a worked notebook.
fn pen(paths) => <g fill:"none", stroke:"#8e704a", 'stroke-width':.85,
    'stroke-linecap':"round", 'stroke-linejoin':"round",
    for (p in paths) <g <path d:p>;
        <path d:p, transform:"translate(.7 -.5)", opacity:".25", 'stroke-width':.55>>>
fn hatching(x,y,count,length,angle = -35) => <g
    transform:"translate(" ++ string(x) ++ " " ++ string(y) ++ ") rotate(" ++ string(angle) ++ ")",
    for (i in 0 to count-1) <path d:"M" ++ string(i*4) ++ " 0 v" ++ string(length),
        fill:"none", stroke:"#8e704a", 'stroke-width':.65>>
fn manuscript(x,y,lines,angle = -4,size = 13) => <g
    transform:"translate(" ++ string(x) ++ " " ++ string(y) ++ ") rotate(" ++ string(angle) ++ ")",
    for (i,words in lines) label(0,i*15,size,words,"#876b47",
        {'font-family':"Apple Chancery, Georgia, serif",'font-style':"italic"})>
fn wheel_study() => <g transform:"translate(111 184) rotate(-12)",
    (pen(["M-49 0 H49 M0 -49 V49", "M-35 -35 L35 35 M-35 35 L35 -35",
        "M-47 -42 H43 M-47 -46 v8 M43 -46 v8", "M-37 48 H37 M-37 44 v8 M37 44 v8"]));
    for (r in [8,28,37]) <circle r:r, fill:"none", stroke:"#8e704a", 'stroke-width':.9>;
    for (i in 0 to 15) <path d:"M-3 -37 v-5 h6 v5", transform:"rotate(" ++ string(i*22.5) ++ ")",
        fill:"none", stroke:"#8e704a", 'stroke-width':.85>;
    (hatching(-22,18,10,13,-48));
    (manuscript(9,-45,["r"],0,11));
    (manuscript(-18,64,["rota dentata"],0,12))>
fn wing_study() => <g transform:"translate(578 126) rotate(8)",
    (pen(["M-91 16 Q-52 -22 -8 -12 Q15 -49 83 -38 Q65 -19 39 -6 Q16 7 -8 9 Q-44 6 -91 16 Z",
        "M-91 16 Q-49 -9 -8 -4 Q28 -30 83 -38", "M-8 -12 L-8 24 M-13 24 H-3",
        "M-70 9 L-56 -6 M-50 6 L-40 -11 M-30 4 L-24 -13 M9 2 L19 -27 M27 -3 L39 -34 M46 -12 L58 -37",
        "M-93 35 H83 M-93 31 v8 M83 31 v8", "M-22 -37 Q-6 -30 7 -40 M1 -42 l6 2 -3 6"]));
    (hatching(42,-25,8,9,-58));
    (manuscript(-63,54,["studio dell’ala"],-3,12))>
fn pendulum_study() => <g transform:"translate(100 176) rotate(-9)",
    (pen(["M-42 -42 H49 M4 -42 V43 M4 -42 L-29 28 M4 -42 L40 29",
        "M-37 19 Q2 63 49 18", "M-39 14 l2 5 6 -1 M43 17 l6 1 -1 -6",
        "M-11 -36 Q4 -28 20 -36", "M-50 59 H51", "M-47 55 v8 M48 55 v8"]));
    for (p in [[-29,28],[4,43],[40,29]]) <circle cx:p[0], cy:p[1], r:7, fill:"none", stroke:"#8e704a">;
    (hatching(-43,-47,18,7,0));
    (manuscript(-35,77,["del moto e del peso"],0,12))>
fn falling_study() => <g transform:"translate(89 342) rotate(-7)",
    (pen(["M-28 -51 V39 H103", "M-16 -39 Q29 -49 86 28",
        "M-18 -51 H20 M-17 -47 H19", "M-16 -39 L-16 39 M16 -35 V39 M48 -19 V39 M80 18 V39",
        "M-33 -48 l5 -5 4 5 M98 35 l5 4 -5 4"]));
    for (p in [[-16,-39],[16,-35],[48,-19],[80,18]]) <circle cx:p[0], cy:p[1], r:4, fill:"none", stroke:"#8e704a">;
    (hatching(-27,45,24,8,0));
    (manuscript(-12,75,["il peso discende"],0,12))>

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
        leader:"M428 220 Q404 222 388 214 M394 213 l-6 1 4 4"}
]
fn background_notes(game) {
    let note = MARGIN_NOTES[game.stage];
    <g 'aria-hidden':"true", 'pointer-events':"none",
        <g opacity:".38",
            (manuscript(41,117,["Of small spheres", "and their diversions"],-6,14));
            (if (game.stage % 2 == 0) wheel_study() else pendulum_study());
            (wing_study());
            (manuscript(494,63,["studies for a curious machine"],3,12))>;
        (if (len(model.targets(game)) == 2) <g opacity:".27", (falling_study())>);
        <g opacity:".63", (manuscript(note.x,note.y,note.lines,note.angle)); (pen([note.leader]))>;
        <g opacity:".35",
            (manuscript(29,478,["λ · fol. " ++ string(game.stage+1)],-90,11));
            (pen(["M678 299 v70 M674 299 h8 M674 369 h8 M680 308 l3 3 -3 3 M680 353 l3 3 -3 3"]))>>
}
fn gear(node, bit) => <g transform:"translate(" ++ string(node.x) ++ " " ++ string(node.y) ++ ")",
    for (i in 0 to 11) <rect x:-3, y:-23, width:6, height:9, rx:1, fill:"#ac8b4e",
        transform:"rotate(" ++ string(i*30) ++ ")">;
    <circle r:18, fill:"url(#brass)", stroke:"#7d653e", 'stroke-width':1.5>;
    <circle r:12, fill:"#e9dab7", stroke:"#a88c58">;
    <path d:if (bit == 0) "M0 -12 L-12 10" else "M0 -12 L12 10", stroke:"#625036",
        'stroke-width':6, 'stroke-linecap':"round">;
    <path d:if (bit == 0) "M0 -12 L-12 10" else "M0 -12 L12 10", stroke:"#f4d99b",
        'stroke-width':3, 'stroke-linecap':"round">;
    <circle r:4, fill:"#746046">;
    (label(29,5,13,node.label))>
fn pocket(node, filled) => <g
    <path d:"M" ++ string(node.x-29) ++ " 417 v32 q29 22 58 0 v-32", fill:"url(#brass)", stroke:"#8b7450", 'stroke-width':2>;
    <ellipse cx:node.x, cy:419, rx:29, ry:10, fill:"#8b7757", stroke:"#d2bc89", 'stroke-width':3>;
    <ellipse cx:node.x, cy:419, rx:21, ry:6, fill:model.INKS[node.color], opacity:".7">;
    if (filled > 0) ball(node.x,429,node.color,12);
    (label(node.x,479,12,upper(node.label),"#75654c",{'text-anchor':"middle",'letter-spacing':1}));
    for (i in 0 to node.need-1) <circle cx:node.x+(i-(node.need-1)/2)*13, cy:494, r:3.5,
        fill:if (i < filled) model.INKS[node.color] else "#d7c8a5", stroke:model.INKS[node.color]>>
fn device(node, bit, filled) {
    if (node.kind == "switch") gear(node,bit)
    else if (node.kind == "bin") pocket(node,filled)
    else if (node.kind == "gate") <g
        <circle cx:node.x, cy:node.y, r:21, fill:"#e8d6ac", stroke:"#a0844f", 'stroke-width':3>;
        <path d:"M" ++ string(node.x-17) ++ " " ++ string(node.y) ++ (if (bit == 0) " h34" else " l30 -24"),
            stroke:"#8d6744", 'stroke-width':7, 'stroke-linecap':"round">;
        if (bit == 1) ball(node.x,node.y+2,6,7);
        (label(node.x+31,node.y+5,11,if (bit == 0) "CLOSED" else "OPEN"))>
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
        <rect width:720, height:518, rx:8, fill:"url(#paper)">;
        for (x in 0 to 23, y in 0 to 16) <circle cx:15+x*30, cy:15+y*30, r:.7, fill:"#ae9c76", opacity:".35">;
        <rect x:10, y:10, width:700, height:498, rx:4, fill:"none", stroke:"#cbb991">;
        (screw(20,20)); (screw(700,20)); (screw(20,498)); (screw(700,498));
        (label(36,92,12,"fig. " ++ string(game.stage+1),"#9b8966",{'font-style':"italic"}));
        (background_notes(game));
        for (node in machine.nodes where node.kind == "relay") <path
            d:"M" ++ string(node.x+22) ++ " " ++ string(node.y) ++ " H" ++ string(machine.nodes[node.link].x) ++
                " V" ++ string(machine.nodes[node.link].y-26),
            fill:"none", stroke:"#b29763", 'stroke-width':1.5, 'stroke-dasharray':"4 5">;
        for (i,node in machine.nodes, j,destination in node.exits) {
            let path = model.rail(node,machine.nodes[destination])
            let live = node.kind != "switch" or game.bits[i] == j;
            <g fill:"none", 'stroke-linecap':"round",
                <path d:path, stroke:"#b7a176", 'stroke-width':15, opacity:".35", transform:"translate(1 3)">;
                <path d:path, stroke:"#8e7956", 'stroke-width':12>;
                <path d:path, stroke:if (live) "#e8cb8f" else "#b9aa8b", 'stroke-width':9>;
                <path d:path, stroke:if (live) "#fbefca" else "#c8b998", 'stroke-width':3>>
        };
        for (i,node in machine.nodes) device(node,game.bits[i],game.filled[i])
    >
}
// The apparatus changes only at a device or pocket; rolling needs just one small overlay.
pub fn apparatus(game) => media.data_uri(binary(format(drawing(game),'xml')^),"image/svg+xml")^
pub fn apparatus_key(game) => {stage:game.stage,bits:game.bits,filled:game.filled}
fn rolling(game) {
    let active = game.active
    let nodes = model.level(game).nodes
    let pos = model.point(nodes[active.from],nodes[active.to],active.tick/model.EDGE_TICKS);
    <div class:"rolling-marble", 'aria-hidden':"true", style:"left:" ++ string(pos.x/720*100) ++
        "%;top:" ++ string(pos.y/518*100) ++ "%;background:radial-gradient(circle at 30% 25%,#fff," ++
        model.INKS[active.color] ++ " 42%,#353139 100%)">
}
fn button(id, command, caption, blocked = false, value = 0) => <button id:id, type:"button",
    'data-action':command, 'data-value':string(value), *:(if (blocked) {disabled:""} else {}), caption>
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

pub fn page(game, apparatus, audio) => <main id:"marble-drop", class:"page", 'data-mode':game.mode,
    'data-level':string(game.stage+1), 'data-active':string(game.active != null),
    'data-paused':string(game.paused), 'data-autoplay':string(game.autoplay),
    'data-collected':string(model.collected(game)), 'data-moves':string(game.moves), 'data-losses':string(game.losses),
    'data-sound':sound.status(audio), 'data-sound-count':string(audio.played), 'data-last-sound':audio.last,
    'data-sound-voices':string(len(audio.voices)),
    <header class:"masthead",
        <div class:"brand", <span class:"brand-icon", "λ">;
            <div <p class:"eyebrow", "THE MECHANICAL PUZZLE COLLECTION">
                <h1 "Marble Drop" <span ".">>>>;
        <span class:"edition", "EIGHT LITTLE INVENTIONS">>;
    <nav class:"levels", 'aria-label':"Choose a puzzle",
        <span class:"eyebrow", "INVENTIONS">;
        (for (i in 0 to len(model.LEVELS)-1) <button id:"level-" ++ string(i+1), type:"button",
            class:"level" ++ (if (game.stage == i) " current" else "") ++ (if ((game.medals[i] or 0) > 0) " completed" else ""),
            'data-action':"level", 'data-value':string(i), title:model.LEVELS[i].title,
            'aria-label':"Puzzle " ++ string(i+1) ++ ": " ++ model.LEVELS[i].title,
            'aria-current':if (game.stage == i) "step" else "false", string(i+1)>);
        <span class:"level-caption", "NO. " ++ string(game.stage+1) ++ " / 08">>;
    <div class:"workbench",
        <section class:"machine-panel",
            <div class:"machine-top", <span "THE APPARATUS"> <span "BRASS · GLASS · A LITTLE GRAVITY">>;
            <div class:"drawing", <img class:"machine-svg", src:apparatus,
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
            (tray(game));
            <div class:"drop-row", <span "Selected: " ++ model.COLORS[game.selected]>;
                (if (len(model.level(game).funnels) > 1) <div class:"inlet-picker",
                    for (i,index in model.level(game).funnels) <button 'data-action':"funnel", 'data-value':string(i),
                        class:if (game.funnel == i) "chosen" else "", "Inlet " ++ model.level(game).nodes[index].label>>);
                (button("drop","drop","Drop marble  ↓",game.active != null or game.paused or game.autoplay or game.mode == "won" or game.stock[game.selected] == 0))>>;
        (notebook(game))>
    <footer <span "1–8 color · A / B drop · Space drop · U undo · H hint · P pause · M sound · R restart">
        <span "MADE OF LAMBDA, NOT MAGIC.">>>
