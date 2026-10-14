import data: .mod_data
import scene: .mod_scene
import assets: .mod_assets

pub fn button(id,title,action,pressed=false) => <button id:id,type:"button",'data-action':action,
    'aria-pressed':string(pressed),title:title,title>
// HTML dimension attributes are text; typed numeric arrays belong to Scene3D.
fn thumb(cell) => <img width:"48",height:"44",src:assets.thumbnail(cell.id),alt:"">
fn specimen(cell,index) => <button id:"choose-" ++ cell.id,class:"specimen",type:"button",
    'data-cell':cell.id,'aria-pressed':string(index == 0),
    thumb(cell);<span class:"specimen-copy",<strong cell.name><small cell.type>>
    <span class:"specimen-number", "0" ++ string(index+1)>
>
fn info(cell) => <div id:"info-" ++ cell.id,class:"cell-info",style:if (cell.id == "plant") "" else "display:none",
    <p class:"eyebrow","ANATOMY INDEX">
    <h2 "Inside the " ++ lower(cell.name)>
    <div class:"organelle-list",*[for (o in cell.organelles)
        <button id:"part-" ++ cell.id ++ "-" ++ o.id,type:"button",'data-part':o.id,
            'aria-pressed':string(o.id == cell.defaultOrganelle),
            <span class:"swatch",style:"background:" ++ o.color><span o.name><span class:"part-arrow","↗">>]>
    ; *[for (o in cell.organelles) <section id:"detail-" ++ cell.id ++ "-" ++ o.id,class:"detail",
        style:if (o.id == cell.defaultOrganelle) "" else "display:none",
        <div class:"detail-heading",<span class:"swatch large",style:"background:" ++ o.color><h3 o.name>>
        <p class:"subtitle",o.subtitle>
        <dl *[for (a in o.attributes) <div <dt a.label><dd a.value>>]>
        <p class:"note",o.note><div class:"fact",<span "DID YOU KNOW?"> <p o.fact>>
    >]
>

pub fn tree() element^ => <main id:"cellscope",'data-cell':"plant",'data-part':"nucleus",'data-compare':"false",'data-loading':"true",
    <div id:"model-loading",class:"model-loading",role:"status",'aria-live':"polite",'aria-atomic':"true",
        <div class:"loading-card",
            <p class:"loading-label","Loading 3D specimen">
            <h3 id:"loading-specimen","Plant Cell">
            <div class:"loading-track",role:"progressbar",'aria-label':"Loading 3D specimen",
                <div class:"loading-bar">>
            <p class:"loading-caption","Loading model and textures…">>>
    <header class:"topbar",
        <div class:"brand",<div class:"brand-mark","◉"><div <h1 "CellScope"><p "Explore life at the microscopic level">>>
        <div class:"top-actions",(button("gallery","Gallery","gallery",true));(button("library","Saved specimens","library"));
            <span class:"edition","LAMBDA / NATIVE 3D">>
    >
    <div class:"workspace",
        <aside class:"catalog panel",
            <div class:"section-heading",<p class:"eyebrow","SPECIMEN COLLECTION"><span string(len(data.cells))>>
            <h2 "Small worlds."<br>"Big discoveries.">
            <div class:"specimens",*[for (i,c in data.cells) specimen(c,i)]>
            <p id:"empty",style:"display:none","No saved specimens.">
            <div class:"catalog-note",<span class:"eyebrow","THE LIVING ATLAS"><p "Explore the architecture of life, from cell walls to muscle fibers.">>
        >
        <section class:"stage-column",
            <section class:"stage panel",
                <div class:"stage-heading",<div <p id:"cell-kind",class:"eyebrow",data.cells[0].type><h2 id:"cell-title","Plant Cell">>>
                <div class:"scene-pair",id:"scene-pair",'aria-busy':"true",
                    <div class:"viewport-wrap",<span class:"scene-badge",id:"scene-label","01 / PLANT CELL">; (scene.viewport("cell-scene")^)>
                    <div class:"viewport-wrap",id:"peer-wrap",style:"display:none",
                        <span class:"scene-badge",id:"peer-label",upper(data.cell(data.cells[0].comparison).name)>
                        ; (scene.viewport("peer-scene")^)>
                >
                <div id:"model-error",class:"model-error",style:"display:none",<p id:"model-error-text">
                    ; (button("retry-model","Retry model download","retry"))>
                <div class:"scene-toolbar",*[
                    button("rotate","Auto rotate","rotate"),
                    button("zoom-in","+","zoom-in"),button("zoom-out","−","zoom-out"),button("reset","Reset view","reset")]>
                <p class:"navigation-help","Drag to orbit · Shift-drag to pan · Scroll to zoom · Arrow keys orbit · R resets · Space rotates">
            >
            <div class:"context-row",
                <section class:"panel context",<p class:"eyebrow","WHERE LIFE HAPPENS"><h3 id:"occurrence-title",data.cells[0].occurrence.title>
                    <p id:"occurrence-body",data.cells[0].occurrence.body>>
                <section class:"panel context",<p class:"eyebrow","SIDE BY SIDE"><h3 "Form follows function.">
                    <p "Compare structures across cell types in two independent native views.">
                    <label class:"compare-label",'for':"peer","Compare with">
                    <select id:"peer",*[for (c in data.cells) <option value:c.id,*:(if (c.id == data.cells[0].comparison) {selected:""} else {}),c.name>]>
                    ; (button("compare","Compare specimens","compare"))>
            >
        >
        <aside class:"inspector panel",
            <div class:"section-heading",<p class:"eyebrow","STRUCTURE & FUNCTION">; (button("favorite","☆","favorite"))>
            ; *[for (c in data.cells) info(c)]
        >
    >
    <footer <span id:"status",role:"status","Select a specimen, then explore its organelles.">
        <span "3D CELL ATLAS · ILLUSTRATIVE MODELS">>
>
