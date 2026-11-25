// The page: the cove filling the window, a small title card, the catch badge and a hint line.
import scene: .mod_scene

let FISH_ICON = <svg xmlns: "http://www.w3.org/2000/svg", viewBox: "0 0 24 16", width: "26", height: "18", 'aria-hidden': "true",
    <path d: "M2 8c3-4.5 8-6 12.5-4.2L20 1v14l-5.5-2.8C10 14 5 12.5 2 8z", fill: "none", stroke: "currentColor", 'stroke-width': "1.8", 'stroke-linejoin': "round">
    <circle cx: "6.5", cy: "7.2", r: "1.1", fill: "currentColor">>
pub fn page(st) any^ => <main id: "fishing", class: "fishing",
    scene.scene(st, {style: "display:block;width:100%;height:100%;touch-action:none"})^;
    <header class: "card", <h1 "Fishing"> <p "Willow Cove">>
    <div id: "badge", class: "badge", role: "status", 'aria-label': "Fish caught", FISH_ICON; <span id: "catch-count", "0">>
    <div id: "toast", class: "toast", 'aria-live': "polite", <span id: "toast-text", "">>
    <p id: "hint", class: "hint", "Arrows or WASD to row · Space to cast">
>
