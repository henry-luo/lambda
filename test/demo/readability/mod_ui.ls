// Browser chrome, original page and reader view for the Readability demo.
import model: .mod_model

// Firefox's reader-view glyph: a page with lines of text
fn reader_icon() => <svg class:"icon",viewBox:"0 0 20 20",'aria-hidden':"true",
    <path d:"M5 3h7l4 4v10H5z",fill:"none",stroke:"currentColor",'stroke-width':"1.6",'stroke-linejoin':"round">
    <path d:"M8 9.5h5M8 12h5M8 14.5h3",fill:"none",stroke:"currentColor",'stroke-width':"1.6",'stroke-linecap':"round">>
fn globe_icon() => <svg class:"icon",viewBox:"0 0 16 16",'aria-hidden':"true",
    <circle cx:"8",cy:"8",r:"6",fill:"none",stroke:"currentColor",'stroke-width':"1.3">
    <path d:"M2 8h12M8 2c-2 2-2 10 0 12M8 2c2 2 2 10 0 12",fill:"none",stroke:"currentColor",'stroke-width':"1.3">>
fn arrow_icon(d) => <svg class:"icon",viewBox:"0 0 16 16",'aria-hidden':"true",
    <path d:d,fill:"none",stroke:"currentColor",'stroke-width':"1.6",'stroke-linecap':"round",'stroke-linejoin':"round">>

fn tab(st,page,i) => <button id:"tab-" ++ page.id,type:"button",role:"tab",class:"tab",
    'data-action':"tab",'data-value':string(i),'aria-selected':string(i == st.at),title:page.label,
    <span class:"favicon site-" ++ page.site,page.mark>
    <span class:"tab-title",page.label>>

fn reader_button(page,reading) => <button id:"reader-toggle",type:"button",class:"reader-toggle",
    'data-action':"reader",'aria-pressed':string(reading),
    'aria-label':if (reading) "Close Reader View" else "Enter Reader View",
    title:if (not page.readerable) "Reader View isn't available for this page"
        else if (reading) "Close Reader View (⌘⇧R)" else "Enter Reader View (⌘⇧R)",
    *:(if (page.readerable) {} else {disabled:""}),
    (reader_icon())>

fn toolbar(page,reading) => <div class:"toolbar",
    <div class:"nav-buttons",'aria-hidden':"true",
        <span class:"nav-button",(arrow_icon("M10 3L5 8l5 5"))>
        <span class:"nav-button",(arrow_icon("M6 3l5 5-5 5"))>
        <span class:"nav-button",(arrow_icon("M13 8a5 5 0 1 1-1.6-3.7M13 3v3h-3"))>>
    <div class:"address",
        <span class:"address-icon",(globe_icon())>
        <span id:"address-text",class:"address-text",
            if (reading) <span class:"address-scheme","about:reader?url="> else null;
            <span class:"address-host",page.address>>;
        (reader_button(page,reading))>>

fn reader_settings(st) => <div class:"reader-settings",role:"toolbar",'aria-label':"Reader View settings",
    <button id:"text-smaller",type:"button",class:"setting text-size",'data-action':"smaller",'aria-label':"Smaller text",
        *:(if (st.size == 0) {disabled:""} else {}),"A−">
    <button id:"text-larger",type:"button",class:"setting text-size large",'data-action':"larger",'aria-label':"Larger text",
        *:(if (st.size == len(model.SIZES) - 1) {disabled:""} else {}),"A+">
    <span class:"settings-rule">;
    *[for (choice in model.THEMES) <button id:"theme-" ++ choice,type:"button",class:"setting swatch swatch-" ++ choice,
        'data-action':"theme",'data-value':choice,'aria-pressed':string(st.theme == choice),
        'aria-label':"Use the " ++ choice ++ " theme","Aa">]>

fn article_header(page,article) {
    let published = model.date_label(article.published_time)
    let minutes = string(model.minutes(article)) ++ " min read"
    let details = [for (part in [article.byline,published,minutes] where part != null) part];
    <header class:"reader-header",
        <p id:"reader-site",class:"reader-site",article.site_name or page.address>
        <h1 id:"reader-title",class:"reader-title",article.title>
        <p id:"reader-details",class:"reader-details",join(details," · ")>>
}

fn reader(st,page) {
    let article = page.article;
    <div id:"reader-view",class:"reader theme-" ++ st.theme,
        (reader_settings(st));
        <div class:"reader-scroll",
            <article class:"reader-article",lang:article.lang or "en",dir:article.dir or "ltr",
                style:"font-size:" ++ string(model.SIZES[st.size]) ++ "px",
                (article_header(page,article));
                <div id:"reader-content",class:"reader-content",article.content>
                <footer class:"reader-footer",
                    <span "Simplified by lambda.doc.readability, a port of Mozilla's Readability.js">
                    <button id:"view-original",type:"button",class:"text-button",'data-action':"reader","View original page">>>>>
}

fn original(page) => <iframe id:"page-frame",class:"page-frame",src:page.url,title:page.label>

pub fn page(st,pages) {
    let shown = model.page(st,pages)
    let reading = model.reading(st,pages);
    <main id:"readability-demo",'data-page':shown.id,'data-mode':if (reading) "reader" else "original",
        'data-theme':st.theme,'data-readerable':string(shown.readerable),
        <header class:"chrome",
            <div class:"tab-strip",
                <div class:"window-dots",'aria-hidden':"true",<span class:"dot red"><span class:"dot amber"><span class:"dot green">>
                <nav class:"tabs",role:"tablist",'aria-label':"Open pages",*[for (i,p in pages) tab(st,p,i)]>>;
            (toolbar(shown,reading))>
        <section class:"viewport",if (reading) reader(st,shown) else original(shown)>>
}
