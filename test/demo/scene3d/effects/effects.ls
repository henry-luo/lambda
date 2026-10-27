import dom
import s: lambda.scene3d
import fx: lambda.scene3d.effects
import navigation: ~~._navigation

fn source_image(selected) => if (selected == "svg") "assets/graphic.svg"
    else if (selected == "raster") "assets/landscape.png" else "assets/text.svg"
fn stage(effect, selected, strength) element => s.scene([
    s.camera({id: "camera", type: 'orthographic', view_height: 2.0}),
    s.resources([s.texture(source_image(selected), {id: "artwork", 'max-size': 1024})]),
    s.mesh(s.plane([4.0, 2.0, 1.0]), fx.material(effect.id, "artwork",
        {amount: strength * 0.003}, {id: "effect"})^),
    fx.animate("effect", 8.0)^
], {id: string(effect.id), width: 480, height: 240, camera: "camera", background: "#12232e",
    role: "img", 'aria-label': effect.title ++ " applied to the selected artwork"})

// typed shader maps stay Lambda values when a control rebuilds the gallery (D7.5.3v2).
view <gallery> state selected: "text", strength: 40 {
    <div id: "gallery", 'data-source': selected, 'data-strength': string(strength),
        <section class: "workbench", 'aria-label': "Effect controls",
            <div class: "source-controls", <span class: "control-label", "01 / SOURCE">
                <div class: "button-group", role: "group", 'aria-label': "Source image",
                    for (choice in [{id: "text", label: "Typography"}, {id: "svg", label: "SVG artwork"}, {id: "raster", label: "Raster image"}])
                        <button id: "source-" ++ choice.id, type: "button", 'data-source': choice.id,
                            'aria-pressed': string(selected == choice.id), choice.label>>>
            <div class: "strength-controls", <label class: "control-label", for: "strength", "02 / INTENSITY">
                <input id: "strength", type: "range", min: "0", max: "100", value: string(strength), 'aria-label': "Effect intensity">
                <output id: "strength-value", for: "strength", string(strength) ++ "%">>>
        <section class: "effect-grid", 'aria-label': "Animated effects",
            for (i, effect in fx.catalog()) <article class: "effect-card",
                <div class: "card-heading",
                    <div <h2 effect.title> <p class: "card-caption", effect.description>>
                    <span "0" ++ string(i + 1)>>;
                stage(effect, selected, strength)>;
            <article class: "source-card",
                <div class: "card-heading", <div <h2 "The original">
                    <p class: "card-caption", "The same artwork, before it starts to move.">> <span "SOURCE">>
                <div class: "original-stage", <img id: "original-image", src: source_image(selected), width: 480, height: 240, alt: "Original source without an effect">>>>>
}
on click(evt) {
    let choice = dom.get_attribute(evt.target, "data-source")
    if (choice != null and contains(["text", "svg", "raster"], choice)) { selected = choice; return 'handled' }
    return 'pass'
}
on input(evt) {
    if (dom.get_attribute(evt.target, "id") == "strength") {
        strength = max(0, min(100, int(dom.get_state(evt.target, "value"))))
        return 'handled'
    }
    return 'pass'
}

<html lang: "en",
    <head <meta charset: "utf-8"> <meta name: "viewport", content: "width=device-width,initial-scale=1">
        <title "Make waves / Scene3D">
        <link rel: "stylesheet", href: "../demo.css"> <link rel: "stylesheet", href: "effects.css">>
    <body <main class: "effects-page",
        navigation.breadcrumbs("Make waves", "../index.html");
        <header class: "page-heading",
            <div <p class: "eyebrow", "SCENE3D / MATERIAL STUDIES">
                <h1 "Make waves.">
                <p class: "intro", "Five ways to set an image in motion.">>
            <p class: "heading-note", "A little distortion. A different perspective.">>
        <div class: "gallery-status", <p "One source. Five interpretations.">
            <div class: "playback-controls",
                <span id: "playback-status", role: "status", "● In motion">
                <button id: "play", type: "button", 'aria-pressed': "true", "Pause">
                <button id: "restart", type: "button", "Restart">>>
        apply(<gallery>);
        <footer class: "page-footer", <span "Text, paths, gradients, pixels. Bring your own image.">
            <a href: "https://blotter.js.org/#/materials", "Inspired by Blotter ↗">>>
        <script src: "effects.js">>
>
