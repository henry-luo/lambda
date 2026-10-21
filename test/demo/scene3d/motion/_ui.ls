// the shared page shell of the motion demos: breadcrumbs back to the Scene3D
// collection, a title and introduction, then the page's own (templated) stage.
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

// one button per controller state; the playing one is marked
pub fn buttons(choices, playing) element => <div class: "toolbar", role: "group", 'aria-label': "Animations",
    for (c in choices) <button id: "play-" ++ c[0], type: "button", class: (if (c[0] == playing) "active" else ""), c[1]>>
