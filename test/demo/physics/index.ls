import scenes: ._scenes
import ui: ._ui

fn preview(kind) element {
    let source=if (kind == "domino") scenes.domino() else if (kind == "salvage") scenes.salvage()
        else if (kind == "sorter") scenes.sorter() else scenes.bounce();
    <scene3d *:source,id:"preview-"++kind,width:500,height:250,
        *[for (node in content(source)) if (name(node) == 'physics') <physics *:node,state:'paused'> else node]>
}

<html ui.head("Physics playground");
    <body <main
        <header class:"page-header", <div <p class:"eyebrow", "LAMBDA PHYSICS / PLAYGROUND"> <h1 "Small worlds. Real motion.">>
            <span class:"edition", "FOUR EXPERIMENTS · 3D">>
        ui.navigation("index");
        <p class:"intro", "Chain reactions, weightless assemblies, a mechanical sorter, and a measured bounce experiment. Open a world, change its conditions, and explore it from any angle.">
        <div class:"gallery",for (demo in ui.demos) <article class:"experiment",
            preview(demo.id);
            <div class:"card-content", <span class:"tag",demo.tag> <h2 demo.title> <p demo.caption>
                <a href:demo.file,"Open experiment →">>>>
        <p class:"hint", "Each experiment includes pause, frame stepping, reset, and an orbit camera.">
    >>
>
