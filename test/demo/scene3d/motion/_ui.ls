// the shared shell of the motion demos: breadcrumbs back to the Scene3D
// collection, a title and introduction, then the page's own (templated) stage.
// Each page lists its choices as {id, label, clip, once}: one button and one
// controller state each; a `once` state plays through and returns to `rest`.
import motion: lambda.scene3d.motion
import collection: ~~._navigation

pub fn page(title, eyebrow, description, stage) element => <html
    <head <meta charset: "utf-8"> <meta name: "viewport", content: "width=device-width,initial-scale=1">
        <title title ++ " / Scene3D"> <link rel: "stylesheet", href: "../demo.css"> <link rel: "stylesheet", href: "motion.css">>
    <body <main class: "scene3d-page motion-page",
        collection.breadcrumbs(title, "../index.html");
        <p class: "eyebrow", eyebrow>
        <h1 title>
        <p class: "intro", description>
        stage>>
>

// one button per choice; the requested one is marked
pub fn buttons(choices, requested) element => <div class: "toolbar", role: "group", 'aria-label': "Animations",
    for (c in choices) <button id: "play-" ++ c.id, type: "button", class: (if (c.id == requested) "active" else ""), c.label>>

pub fn states(choices, rest) => [for (c in choices)
    if (c.once == true) motion.motion_state(symbol(c.id), c.clip, {loop: 'once', next: rest, fade: 0.15})
    else motion.motion_state(symbol(c.id), c.clip)];

// the template's [state, cue] after a click on `id`: a choice's button switches to it,
// and the requested one's button cues it again
pub fn pick(id, choice, cue) => if (id == null or not starts_with(id, "play-")) [choice, cue]
    else (let next = symbol(slice(id, 5, len(id))), if (next == choice) [choice, cue + 1] else [next, cue])
