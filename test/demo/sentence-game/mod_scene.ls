import model: .mod_model
import art: .mod_art

let PILES = {
    subject:{title:"Who is it about?",caption:"Pick a subject"},
    predicate:{title:"What do they do?",caption:"Pick a predicate"},
    object:{title:"Who or what?",caption:"Pick an object"}
}
let PART_NAMES = {subject:"Subject",predicate:"Predicate",object:"Object"}

fn card_tile(card,kind,selected,locked) => <button id:kind ++ "-" ++ card.id,type:"button",class:"card",
    'data-action':kind,'data-card':card.id,'aria-pressed':string(selected == card.id),
    *:(if (locked) {disabled:""} else {}),
    <span class:"card-art",style:"--art-bg:" ++ card.color,(art.picture(card.art))>
    <span class:"card-copy",<span class:"card-name",card.text><span class:"card-hint",card.hint>>
>
fn pile(st,kind) {
    let caption = if (kind == "predicate" and st.mode == 3) "Pick a verb" else PILES[kind].caption;
    <section class:"card-column",'aria-labelledby':kind ++ "-heading",
        <div class:"section-heading",<h2 id:kind ++ "-heading",PILES[kind].title><span id:kind ++ "-caption",class:"section-tag",caption>>
        <div id:kind ++ "-cards",class:"card-grid",role:"group",'aria-label':caption,
            *[for (card in st.hands[kind]) card_tile(card,kind,st[kind],st.phase != "build")]>
    >
}
fn part_name(mode,kind) => if (mode == 3 and kind == "predicate") "Verb" else PART_NAMES[kind]
fn formula(mode) => <div class:"formula",'aria-label':"Sentence parts",
    *[for (i,kind in model.kinds(mode)) <span class:"formula-part",
        if (i > 0) <b "+">;
        <span (part_name(mode,kind))>>];
    <b "="><strong "Sentence!">
>
fn mode_bar(st) => <section class:"mode-bar",'aria-label':"Sentence challenge",
    <div <p class:"eyebrow","Choose your challenge">
        <p class:"mode-help",if (st.mode == 2) "A subject + a complete predicate."
            else "Split the predicate into a verb + an object.">>
    <div class:"mode-options",role:"group",'aria-label':"Number of parts per clause",*[for (mode in [2,3])
        <button id:"mode-" ++ string(mode),type:"button",class:"mode-button",'data-action':"mode",'data-card':string(mode),
            'aria-pressed':string(mode == st.mode),*:(if (st.phase != "build") {disabled:""} else {}),
            string(mode) ++ " parts">]>
>
fn clause_view(clause) => <span class:"clause-fragment",
    if (clause.link != "") <span class:"word-connector",model.separator(clause.link)>;
    *[for (i,kind in model.kinds(clause.mode)) <span class:if (model.card(clause,kind) == null) "placeholder" else "word-" ++ kind,
        (if (i > 0) " " else "") ++ (if (model.card(clause,kind) == null) "[" ++ lower(part_name(clause.mode,kind)) ++ "]" else model.part_text(clause,kind))>]
>
fn preview(st) {
    <p class:"sentence-preview",id:"sentence-preview",'aria-live':"polite",
        if (len(st.clauses) == 0 and st.subject == "" and st.predicate == "" and st.object == "")
            <span class:"placeholder","Your words will land here…">
        else <span class:"sentence-text",*[for (clause in model.all_clauses(st)) clause_view(clause)];
            (if (model.complete(st)) "." else "")>
    >
}
fn join_controls(st) => <div class:"join-controls",
    <span class:"join-label","Add a clause:">
    <div class:"connector-options",*[for (connector in model.connectors)
        <button id:"join-" ++ connector.id,type:"button",class:"connector-button",'data-action':"extend",'data-card':connector.id,
            'aria-label':"Add a clause with " ++ connector.text,
            *:(if (st.phase != "build" or not model.complete(st)) {disabled:""} else {}),connector.text ++ " +">]>
    <button id:"undo-join",type:"button",class:"undo-join",'data-action':"undo-join",
        *:(if (st.phase != "build" or st.previous == null) {disabled:""} else {}),"↶ Undo join">
>
fn sentence_zone(st) => <section class:"sentence-zone",'aria-labelledby':"sentence-heading",
    <div class:"sentence-topline",
        <h2 id:"sentence-heading","Your sentence">
        <span class:"sentence-instruction",if (st.phase == "saved") "Sentence saved. Try another!"
            else "Clause " ++ string(len(st.clauses)+1) ++ " · Choose " ++ string(st.mode) ++ " cards.">
    >
    ; (preview(st));
    (join_controls(st));
    <div class:"sentence-actions",
        <div id:"feedback",class:"feedback" ++ (if (st.phase == "saved")
            (if (st.book[len(st.book)-1].natural) " good" else " wacky") else ""),role:"status",'aria-live':"polite",model.feedback(st)>
        <button id:"primary",type:"button",class:"primary-button",'data-action':"primary",
            *:(if (st.phase == "build" and not model.ready(st)) {disabled:""} else {}),
            if (st.phase == "build") "Make my sentence →"
            else if (len(st.book) == model.rounds) "Open my sentence book →" else "Next sentence →">
    >
>
fn collection(st) => <section class:"book-panel",'aria-labelledby':"book-heading",
    <div class:"book-header",<h2 id:"book-heading","Your sentence book"><span class:"book-count",string(len(st.book)) ++ " saved">>
    if (len(st.book) == 0) <p class:"empty-book","Your stories will collect here as you play.">
    else <div class:"sentence-list",*[for (entry in st.book)
        <span class:"sentence-chip",<span class:"chip-star",'aria-hidden':"true","★">entry.text>]>
>
fn finish(st) => <section class:"finish-screen show",id:"finish",'aria-labelledby':"finish-heading",
    <div class:"finish-emoji",(art.picture("book"))>
    <p class:"eyebrow","Storyteller badge unlocked">
    <h2 id:"finish-heading","You made a tiny book!">
    <p "Five sentences, a little grammar, and a whole lot of imagination. Here’s your sentence collection.">
    <div class:"book-recap",*[for (entry in st.book)
        <div class:"recap-line",<span class:"recap-art",(art.picture(entry.art))><span entry.text>>]>
    <button id:"play-again",type:"button",class:"primary-button",'data-action':"reset","Play another round ↻">
>

pub fn page(st) => <div id:"sentence-game",class:"page-wrap",tabindex:"0",
    'data-phase':st.phase,'data-count':string(len(st.book)),'data-subject':st.subject,'data-predicate':st.predicate,
    'data-mode':string(st.mode),'data-object':st.object,'data-joins':string(len(st.clauses)),
    <header class:"topbar",
        <div class:"brand",<span class:"brand-mark",'aria-hidden':"true","Aa"><span "Sentence Match">>
        <div class:"top-actions",
            <span class:"progress-pill",id:"score",'aria-live':"polite","★ " ++ string(len(st.book)) ++ " of " ++ string(model.rounds) ++ " sentences">
            <button id:"reset",type:"button",class:"quiet-button",'data-action':"reset",'aria-label':"Start over","Start over">
        >
    >
    <main
        <section class:"intro",'aria-labelledby':"page-title",
            <div <p class:"eyebrow","The great sentence mix-up">
                <h1 id:"page-title","Build a " <span class:"highlight","sentence!">>
                <p class:"intro-copy","Start with two or three parts. Add a conjunction to make a bigger story. Make it sensible, make it silly—you’re the storyteller.">
            >
            ; (formula(st.mode))
        >
        ; (mode_bar(st));
        <section class:"game-board",'aria-label':"Sentence building game",
            <div class:"board-ribbon",
                <div class:"round-label",<span class:"sparkle",'aria-hidden':"true","✦">
                    <span id:"round-label",if (st.phase == "done") "All done!" else "Round " ++ string(st.round) ++ " of " ++ string(model.rounds)>>
                <div class:"round-dots",'aria-label':"Game progress",*[for (i in 0 to (model.rounds-1))
                    <span class:"round-dot" ++ (if (i < len(st.book)) " done" else if (i == st.round-1) " current" else ""),'aria-hidden':"true">]>
            >
            if (st.phase == "done") finish(st)
            else <div id:"play-screen",
                <div class:"card-area" ++ (if (st.mode == 3) " three-parts" else ""),
                    *[for (kind in model.kinds(st.mode)) pile(st,kind)]>
                ; (sentence_zone(st));(collection(st))
            >
        >
        <p class:"footer-note","A subject tells who. A verb tells the action. An object tells who or what receives it. A conjunction joins clauses."
            <br>"Keyboard: A / S / D subject · J / K / L predicate or verb · Z / X / C object · Tab and Enter use the buttons.">
    >
>
