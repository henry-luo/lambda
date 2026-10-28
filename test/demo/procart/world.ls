// Run: ./lambda.exe view test/demo/procart/world.ls
// A world from one seed, built with lambda.procart: eroded terrain, rivers and
// biomes; towns placed by Poisson sampling and named by a Markov model; a line of
// lore from a Tracery-style grammar; an emblem grown by a stochastic L-system; and
// the floor plan of a ruin from a BSP dungeon.
import dom
import resources: ~~.mod_resources
import W: .mod_world
import draw: .mod_map
import ui: .mod_ui

let BASE = url_resolve(resources.entry_uri()^, "./")
let CSS = input(url_resolve(BASE, "world.css"), 'text')^

fn initial() => {seed: 2026, rivers: true, erosion: true, climate: false, world: W.generate(2026)^}

fn rebuilt(st, seed, erosion) => {*: st, seed: seed, erosion: erosion,
    world: W.generate(seed, {erosion: erosion, climate: st.climate})^}

fn command(st, action) {
    if (action == "next") rebuilt(st, st.seed + 1, st.erosion)
    else if (action == "previous") rebuilt(st, max(0, st.seed - 1), st.erosion)
    else if (action == "rivers") {*: st, rivers: not st.rivers}
    else if (action == "erosion") rebuilt(st, st.seed, not st.erosion)
    else if (action == "climate") rebuilt({*: st, climate: not st.climate}, st.seed, st.erosion)
    else st
}

fn toggle(action, label, on) => <button type:"button", class:(if (on) "toggle on" else "toggle"), 'data-act':action, label>

view <world_app> state st: initial() {
    <body <div class:"app",
        <header class:"bar",
            <div class:"title", <h1 st.world.realm> <span class:"seed", "seed " ++ string(st.seed)>>
            ui.navigation("world");
            <div class:"actions", *[<button type:"button", class:"step", 'data-act':"previous", "‹ Previous">,
                <button type:"button", class:"step primary", 'data-act':"next", "New world ›">,
                toggle("rivers", "Rivers", st.rivers), toggle("erosion", "Erosion", st.erosion),
                toggle("climate", "Climate", st.climate)]>>
        <main class:"body",
            <section class:"map-frame", draw.world_map(st.world, st.rivers)>
            <aside class:"side",
                <div class:"card lore", *[draw.emblem(st.world.emblem), <p st.world.lore>]>
                <div class:"card", <h2 "Towns">
                    <ol class:"towns", *[for (t in st.world.towns) <li class:(if (t.capital) "capital" else ""), t.name>]>>
                <div class:"card", *[<h2 "The ruins of " ++ st.world.ruin>, draw.floor_plan(st.world.dungeon)]>
                <div class:"card", <h2 "Lands"> <ul class:"legend", *draw.legend(st.world)>>>>>>
}
on click(evt) {
    let btn = dom.closest(evt.target, "[data-act]")
    if (btn == null) { return 'pass' }
    st = command(st, dom.get_attribute(btn, "data-act"))
    return 'handled'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "World — Procart"><style CSS>>
    apply(<world_app>)>
