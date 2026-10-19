import model: .mod_model
import art: .mod_art

pub fn pattern_strip(id) => <span class:"pattern-strip",'aria-label':"Sentence parts",
    *[for (kind in model.kinds(id)) <span class:"part-slot part-" ++ kind,model.part(kind).question>]
>
fn pattern_choice(st,pattern) => <button id:"pattern-" ++ pattern.id,type:"button",class:"pattern-choice",
    'data-action':"pattern",'data-card':pattern.id,'aria-pressed':string(st.pattern == pattern.id),
    <span class:"pattern-topline",<strong pattern.title><span class:"pattern-count",string(len(pattern.parts)) ++ " parts →">>
    ; (pattern_strip(pattern.id));
    <span class:"pattern-example",pattern.example>
>
fn chooser(st) => <section id:"pattern-screen",class:"pattern-screen",'aria-labelledby':"pattern-heading",
    <div class:"chooser-heading",<h2 id:"pattern-heading","Choose your sentence recipe">
        <span class:"section-tag",string(len(model.patterns)) ++ " patterns · " ++ string(model.card_count) ++ " picture cards">>
    if (st.pattern != "") <div class:"draft-note",
        <span "Your current clause is waiting. Pick the same pattern to keep your cards, or try a new one.">
        <button id:"resume",type:"button",class:"text-button",'data-action':"resume","Back to my sentence →">>;
    *[for (group in [{title:"Core patterns",advanced:false},{title:"More detail: how and why",advanced:true}])
        <section class:"pattern-group",<h3 group.title>
            <div class:"pattern-grid",*[for (pattern in model.patterns where (pattern.advanced == true) == group.advanced) pattern_choice(st,pattern)]>>];
    (collection(st))
>
// `data-say` holds the words a double-click reads aloud (sentence-game.ls).
fn card_tile(card,kind,selected,locked,key="") => <button id:kind ++ "-" ++ card.id,type:"button",class:"card kind-" ++ kind,
    'data-action':kind,'data-card':card.id,'data-say':card.text,'aria-pressed':string(selected == card.id),
    *:(if (locked) {disabled:""} else {}),
    *:(if (key != "") {'aria-keyshortcuts':key} else {}),
    <span class:"card-art",style:"--art-bg:" ++ card.color,(art.picture(card.art))>
    <span class:"card-copy",<span class:"card-name",card.text><span class:"card-hint",card.hint>
        if (key != "") <span class:"card-key",'aria-hidden':"true",key>>
>
pub fn pile(st,kind) {
    let info = model.part(kind);
    <section class:"card-column kind-" ++ kind,'aria-labelledby':kind ++ "-heading",
        <div class:"section-heading",<h2 id:kind ++ "-heading",<span class:"part-dot",'aria-hidden':"true">info.question>
            <span id:kind ++ "-caption",class:"section-tag",info.caption>
            <span class:"key-hint",upper(join([for (i,key in info.keys where i < len(st.hands[kind])) key]," / "))>>
        <div id:kind ++ "-cards",class:"card-grid",role:"group",'aria-label':info.caption,
            *[for (card in st.hands[kind]) card_tile(card,kind,st[kind],st.phase != "build")]>
    >
}
pub fn card_area(st) => <div class:"card-area",
    if (st.mixed_cards == null) *[for (kind in model.kinds(st.pattern)) pile(st,kind)]
    else <section class:"mixed-bank",'aria-labelledby':"mixed-heading",
        <div class:"mixed-bank-heading",<h2 id:"mixed-heading","All cards, mixed together">
            <span class:"section-tag",string(len(st.mixed_cards)) ++ " cards">>
        <p class:"mixed-help","Use the colors as clues. Pick the words you need from anywhere in the group.">
        <div id:"mixed-cards",class:"card-grid mixed-cards",role:"group",'aria-label':"Mixed picture cards",
            *[for (i,entry in st.mixed_cards) card_tile(entry.card,entry.kind,st[entry.kind],st.phase != "build",model.bank_keys[i] or "")]>
    >
>
pub fn scramble_button(st) => <button id:"scramble-cards",type:"button",class:"scramble-button",'data-action':"scramble",
    title:"Mix all the cards into one group. Keep the card colors and your sentence.",
    *:(if (st.phase != "build") {disabled:""} else {}),
    <span 'aria-hidden':"true","⇄">; (if (st.mixed_cards != null) " Scramble again" else " Scramble cards")
>
fn pattern_bar(st) => <section class:"pattern-bar",'aria-label':"Selected pattern",
    <div class:"pattern-bar-title",<p class:"eyebrow","Your sentence recipe"><strong model.pattern(st.pattern).title>>
    ; (pattern_strip(st.pattern));
    <div class:"pattern-bar-actions",
        (scramble_button(st));
        <button id:"new-cards",type:"button",class:"text-button",'data-action':"deal",title:"Deal new cards for this clause",
            *:(if (st.phase != "build") {disabled:""} else {}),"↻ New cards">
        <button id:"change-pattern",type:"button",class:"text-button",'data-action':"patterns",
            *:(if (st.phase != "build") {disabled:""} else {}),"← Change pattern">>
>
fn clause_view(clause) => <span class:"clause-fragment",
    if (clause.link != "") <span class:"word-connector",model.separator(clause.link)>;
    *[for (i,kind in model.kinds(clause.pattern)) <span class:if (model.card(clause,kind) == null) "placeholder" else "word-" ++ kind,
        (if (i > 0) " " else "") ++ (if (model.card(clause,kind) == null) "[" ++ lower(model.part_name(clause.pattern,kind)) ++ "]" else model.part_text(clause,kind))>]
>
fn preview(st) => <p class:"sentence-preview",id:"sentence-preview",'aria-live':"polite",
    if (len(st.clauses) == 0 and all([for (kind in model.kinds(st.pattern)) st[kind] == ""]))
        <span class:"placeholder","Your words will land here…">
    else <span class:"sentence-text",*[for (clause in model.all_clauses(st)) clause_view(clause)];
        (if (model.complete(st)) "." else "")>
>
pub fn join_controls(st) => <div class:"join-controls",
    <span class:"join-label","Make it longer:">
    <div class:"connector-options",*[for (connector in model.connectors)
        <button id:"join-" ++ connector.id,type:"button",class:"connector-button",'data-action':"extend",'data-card':connector.id,
            'aria-label':"Add a clause with " ++ connector.text,
            *:(if (st.phase != "build" or not model.complete(st)) {disabled:""} else {}),connector.text ++ " +">]>
    <button id:"undo-join",type:"button",class:"undo-join",'data-action':"undo-join",
        *:(if (st.phase != "build" or st.previous == null) {disabled:""} else {}),"↶ Undo join">
>
fn read_icon(reading) => if (reading) <svg class:"read-icon",width:16,height:16,viewBox:"0 0 24 24",'aria-hidden':"true",
        <rect x:6,y:6,width:12,height:12,rx:2,fill:"currentColor">>
    else <svg class:"read-icon",width:16,height:16,viewBox:"0 0 24 24",'aria-hidden':"true",
        <path d:"M3 9 H7 L12 5 V19 L7 15 H3 Z",fill:"currentColor">
        <path d:"M15.5 9 A4 4 0 0 1 15.5 15 M18 6.5 A7.5 7.5 0 0 1 18 17.5",fill:"none",stroke:"currentColor",'stroke-width':2,'stroke-linecap':"round">>
// the view reads `words` aloud (sentence-game.ls); while that reading lasts, the same button stops it.
pub fn read_button(st,words) {
    let reading = st.reading == true;
    <button id:"read-aloud",type:"button",class:"read-aloud",'data-action':"read",'data-say':words,'aria-pressed':string(reading),
        title:if (reading) "Stop reading" else "Hear your sentence read aloud",
        *:(if (words == "" and not reading) {disabled:""} else {}),
        (read_icon(reading)); (if (reading) "Stop" else "Read aloud")>
}
pub fn sentence_zone(st) => <section class:"sentence-zone",'aria-labelledby':"sentence-heading",
    <div class:"sentence-topline",<h2 id:"sentence-heading","Your sentence">
        <div class:"sentence-tools",<span class:"sentence-instruction",if (st.phase == "saved") "Saved to your book!"
            else "Clause " ++ string(len(st.clauses)+1) ++ (if (st.mixed_cards != null) " · Pick from the mixed cards." else " · Pick one card for each question.")>
            ; (read_button(st,model.sentence(st)))>>
    ; (preview(st))
>
fn sentence_actions(st) => <div class:"builder-actions",
    (join_controls(st));
    <div class:"sentence-actions",
        <div id:"feedback",class:"feedback" ++ (if (st.phase == "saved")
            (if (model.saved_entry(st).natural) " good" else " wacky") else ""),role:"status",'aria-live':"polite",model.feedback(st)>
        <button id:"primary",type:"button",class:"primary-button",'data-action':"primary",
            *:(if (st.phase == "build" and not model.ready(st)) {disabled:""} else {}),
            if (st.phase == "build") "Save my sentence →"
            else if (len(st.book) == model.rounds) "Open my sentence book →" else "Next sentence →">
    >
>
pub fn collection(st) => <section class:"book-panel",'aria-labelledby':"book-heading",
    <div class:"book-header",<h2 id:"book-heading","Your sentence book"><span class:"book-count",string(len(st.book)) ++ " saved">>
    if (len(st.book) == 0) <p class:"empty-book","Your stories will collect here as you play.">
    else <div class:"sentence-list",*[for (entry in st.book)
        <span class:"sentence-chip",<span class:"chip-star",'aria-hidden':"true","★">entry.text>]>
>
pub fn finish(st,title="You made a tiny book!",next_level=2) => <section class:"finish-screen show",id:"finish",'aria-labelledby':"finish-heading",
    <div class:"finish-emoji",(art.picture("book"))>
    <p class:"eyebrow","Storyteller badge unlocked">
    <h2 id:"finish-heading",title>
    <p string(len(st.book)) ++ " sentences, a little grammar, and a whole lot of imagination. Here’s your sentence collection.">
    <div class:"book-recap",*[for (entry in st.book)
        <div class:"recap-line",<span class:"recap-art",(art.picture(entry.art))><span entry.text>>]>
    <div class:"finish-actions",
        <button id:"next-level",type:"button",class:"primary-button",'data-action':"level",'data-card':string(next_level),"Try Level " ++ string(next_level) ++ " →">
        <button id:"play-again",type:"button",class:"text-button",'data-action':"reset","Play this level again ↻">>
>
pub fn round_navigation(st,total,caption="Round") => <div class:"round-navigation",
    <div class:"board-ribbon",
        <div class:"round-label",<span class:"sparkle",'aria-hidden':"true","✦">
            <span id:"round-label",caption ++ " " ++ string(st.round) ++ " of " ++ string(total)>>
        <div class:"round-dots",role:"group",'aria-label':caption ++ " navigation",*[for (id in 1 to total)
            <button id:"round-" ++ string(id),type:"button",class:"round-dot" ++ (if (model.round_saved(st,id)) " done" else "") ++ (if (id == st.round) " current" else ""),
                'data-action':"round",'data-card':string(id),'aria-pressed':string(id == st.round),
                'aria-label':"Go to " ++ lower(caption) ++ " " ++ string(id) ++ (if (model.round_saved(st,id)) ", completed" else ", unfinished"),
                title:caption ++ " " ++ string(id),*:(if (id == st.round) {'aria-current':"step"} else {}),
                <span class:"round-dot-mark",'aria-hidden':"true">>]>
    >
    <p class:"round-nav-help","Click a dot to switch " ++ lower(caption) ++ "s. Your cards and draft stay here.">
>
fn builder(st) => <div id:"play-screen",
    (pattern_bar(st));
    <section class:"game-board",'aria-label':"Sentence building game",
        (sentence_zone(st));
        (card_area(st));
        (sentence_actions(st));(collection(st))
    >
>

pub fn frame(st,level,progress,body_content) => <div id:"sentence-game",class:"page-wrap",tabindex:"0",
    'data-level':string(level),
    'data-phase':st.phase,'data-count':string(len(st.book)),'data-subject':st.subject,'data-predicate':st.predicate,
    'data-pattern':st.pattern,'data-object':st.object,'data-where':st.where,'data-when':st.when,'data-how':st.how,'data-why':st.why,
    'data-round':string(st.round),'data-joins':string(len(st.clauses or [])),
    <header class:"topbar",
        <div class:"brand",<span class:"brand-mark",'aria-hidden':"true","Aa"><span "Sentence Match">>
        <div class:"top-actions",
            <span class:"progress-pill",id:"score",'aria-live':"polite",progress>
            if (level != 0) <button id:"reset",type:"button",class:"quiet-button",'data-action':"reset",'aria-label':"Start this level over","Start over">
        >
    >
    if (level != 0) <nav class:"level-nav",'aria-label':"Game levels",
        <div class:"level-tabs",*[for (id in 1 to 3)
            <button id:"nav-level-" ++ string(id),type:"button",class:"level-tab",'data-action':"level",'data-card':string(id),
                'aria-pressed':string(level == id),"Level " ++ string(id) ++ (if (id == 1) " · Build" else if (id == 2) " · Unscramble" else " · Describe")>]>
        <button id:"choose-level",type:"button",class:"text-button",'data-action':"levels","All levels →">>;
    <main body_content>
>
pub fn hero(lead,highlight,copy) => <section class:"intro",'aria-labelledby':"page-title",
    <div <p class:"eyebrow","The great sentence mix-up"><h1 id:"page-title",lead;<span class:"highlight",highlight>>
        <p class:"intro-copy",copy>>
>
pub fn page(st) => frame(st,1,<span "★ " string(len(st.book)) " of " string(model.rounds) " sentences">,<div
        <div class:"step-track",'aria-label':"Game steps",
            <span class:"step" ++ (if (st.phase == "pattern") " active" else ""),<b "1">"Pick a pattern">
            <span class:"step-arrow",'aria-hidden':"true","→">
            <span class:"step" ++ (if (st.phase == "build" or st.phase == "saved") " active" else ""),<b "2">"Build a sentence">
        >
        ; (hero(if (st.phase == "pattern") "Pick a " else "Build a ",if (st.phase == "pattern") "pattern!" else "sentence!",
            if (st.phase == "pattern") "Start small or tell the whole story. Choose a recipe, then bring it to life with picture cards."
            else "Who’s doing what, where, when, how and why? Mix your cards, then add a conjunction to grow your story."));
        (round_navigation(st,model.rounds));
        if (st.phase == "pattern") chooser(st)
        else if (st.phase == "done") <div class:"game-board",(finish(st))>
        else builder(st);
        <p class:"footer-note","Who · Doing what · To what · Where · When · How · Why — each part has its own color."
            <br>; (if (st.mixed_cards != null) "Keys 1–9 and 0 pick the first ten mixed cards. Use a mouse or Tab and Enter for every card."
                else "Pick cards with a mouse, or use Tab and Enter. Letter shortcuts appear beside each pile.")>
    >)
