import game: .mod_game
import model: .mod_model
import puzzle: .mod_puzzle
import picture: .mod_picture
import base: .mod_scene
import art: .mod_art

fn levels_screen(app) => base.frame({phase:"levels",book:[],clauses:[]},0,"Three ways to play",<div
    (base.hero("Little words. ","Big stories!","Choose a level and grow from sentence builder to picture storyteller. Your progress stays here when you switch levels."));
    <section id:"level-screen",class:"level-grid",'aria-label':"Choose a level",*[for (level in game.levels)
        <button id:"level-" ++ string(level.id),type:"button",class:"level-choice level-color-" ++ string(level.id),
            'data-action':"level",'data-card':string(level.id),
            <span class:"level-number","Level " ++ string(level.id)>
            <span class:"level-art",(art.picture(level.art))>
            <span class:"eyebrow",level.tag><strong class:"level-title",level.title>
            <span class:"level-copy",level.copy><span class:"level-detail",level.detail>
            <span class:"level-go",if (app.sessions[string(level.id)] != null) "Continue playing →" else "Let’s play →">
        >]>
    <p class:"footer-note","Play at your own pace. All three levels use the same sentence-building colors.">
>)
// a word card reads its word on double-click; the full stop is named, not voiced as a pause.
fn spoken(card) => if (card.text == ".") "full stop" else card.text
fn word_button(card,index,locked) => <button id:"word-" ++ card.id,type:"button",class:"word-card",'data-action':"word",'data-card':card.id,
    'data-say':spoken(card),
    *:(if (locked) {disabled:""} else {}),
    <span card.text><span class:"word-key",model.bank_keys[index] or "">
>
fn placed_word(card,index,word_count,locked) => <span class:"placed-word",
    <button type:"button",class:"move-word",'data-action':"word-left",'data-card':card.id,'aria-label':"Move " ++ card.text ++ " left",
        *:(if (locked or index == 0) {disabled:""} else {}),"←">
    <button id:"placed-" ++ card.id,type:"button",class:"placed-label",'data-action':"remove-word",'data-card':card.id,'data-say':spoken(card),
        'aria-label':"Return " ++ card.text ++ " to the word bank",*:(if (locked) {disabled:""} else {}),card.text>
    <button type:"button",class:"move-word",'data-action':"word-right",'data-card':card.id,'aria-label':"Move " ++ card.text ++ " right",
        *:(if (locked or index == word_count-1) {disabled:""} else {}),"→">
>
fn puzzle_play(st) => <section id:"puzzle-screen",class:"game-board",'aria-labelledby':"puzzle-heading",
    <div class:"puzzle-clue",<span class:"clue-art",(art.picture(puzzle.lesson(st).art))>
        <div <h2 id:"puzzle-heading",puzzle.lesson(st).topic><p puzzle.lesson(st).clue>
            <p class:"puzzle-progress",string(len(st.order)) ++ " / " ++ string(len(st.cards)) ++ " cards placed">>>
    <section class:"puzzle-answer",'aria-labelledby':"arrange-heading",
        <div class:"sentence-topline",<h2 id:"arrange-heading","Arrange your sentence">
            <div class:"sentence-tools",<button id:"hint",type:"button",class:"text-button",'data-action':"hint",'aria-pressed':string(st.hint),
                *:(if (st.phase != "build") {disabled:""} else {}),if (st.hint) "Hide hint" else "Need a hint?">
                ; (base.read_button(st,puzzle.assembled(st)))>>
        if (st.hint) <p class:"puzzle-hint","Start with “" ++ join([for (i,word in split(puzzle.lesson(st).answers[0]," ") where i < 2) word]," ") ++ "”. The full stop goes last.">;
        <div id:"ordered-words",class:"ordered-words",role:"group",'aria-label':"Words in your sentence",
            if (len(st.order) == 0) <p class:"empty-order","Click the cards below in sentence order.">
            else *[for (i,card in puzzle.placed(st)) placed_word(card,i,len(st.order),st.phase != "build")]>
        <p id:"sentence-preview",class:"puzzle-reading",'aria-live':"polite",puzzle.assembled(st)>
    >
    <section class:"word-bank",'aria-labelledby':"bank-heading",
        <div class:"chooser-heading",<h2 id:"bank-heading","Scrambled word cards"><span class:"section-tag","Use every card once">>
        <div id:"word-bank",class:"word-bank-cards",*[for (i,card in puzzle.available(st)) word_button(card,i,st.phase != "build")]>
    >
    <div class:"builder-actions",
        <div class:"puzzle-tools",
            <button id:"undo-word",type:"button",class:"text-button",'data-action':"undo-word",
                *:(if (st.phase != "build" or len(st.order) == 0) {disabled:""} else {}),"↶ Undo last card">
            <button id:"clear-words",type:"button",class:"text-button",'data-action':"clear-words",
                *:(if (st.phase != "build" or len(st.order) == 0) {disabled:""} else {}),"Clear the line">
            <span class:"tool-help","Tap a placed word to return it. Use ← / → to move it.">
        >
        <div class:"sentence-actions",<p id:"feedback",class:"feedback" ++ (if (st.phase == "solved") " good" else ""),role:"status",st.notice>
            <button id:"primary",type:"button",class:"primary-button",'data-action':"primary",
                *:(if (st.phase == "build" and not puzzle.full(st)) {disabled:""} else {}),
                if (st.phase == "build") "Check my sentence →"
                else if (len(st.book) == len(puzzle.lessons)) "See my sentences →" else "Next puzzle →">>
    >
    ; (base.collection(st))
>
fn puzzle_page(st) => base.frame(st,2,<span "★ " string(len(st.book)) " of " string(len(puzzle.lessons)) " solved">,<div
    (base.hero("Untangle the ","sentence!","All the words are here. Follow the clue, put the cards in order, then read your sentence aloud."));
    (base.round_navigation(st,len(puzzle.lessons),"Puzzle"));
    if (st.phase == "done") <div class:"game-board",(base.finish(st,"Sentence detective!",3))> else puzzle_play(st);
    <p class:"footer-note","Keyboard: 1–9 and 0 pick cards from the bank · Backspace undoes the last card · Tab and Enter use every button.">
>)
fn picture_picker(st) => <section id:"picture-screen",class:"picture-picker",'aria-label':"Choose a picture",
    <div class:"picture-grid",*[for (scene in picture.pictures)
        <button id:"picture-" ++ scene.id,type:"button",class:"picture-choice",'data-action':"picture",'data-card':scene.id,
            (art.scene(scene.art,scene.alt));
            <span class:"picture-choice-copy",
                <span class:"picture-meta" ++ (if (scene.advanced == true) " advanced" else ""),
                    (if (scene.advanced == true) "Advanced · " else "") ++ string(len(scene.decks.subject)) ++ " subjects · " ++ string(picture.card_count(scene)) ++ " cards">
                <strong scene.title><span "Look closely & tell a story →">>
        >]>;
    (base.collection(st))
>
fn recipe_options(st) => <section class:"picture-recipes",'aria-label':"Sentence recipe",
    <div class:"chooser-heading",<h2 "Pick a sentence recipe"><span class:"reuse-pill","∞ Cards can be used again">>
    <div class:"recipe-options",*[for (recipe in model.patterns)
        <button id:"recipe-" ++ recipe.id,type:"button",class:"recipe-button",'data-action':"recipe",'data-card':recipe.id,
            'aria-pressed':string(st.pattern == recipe.id),recipe.title>]>;
    (base.pattern_strip(st.pattern))
>
fn picture_play(st) {
    let scene = picture.picture(st.scene);
    <div id:"picture-play",
        <div class:"scene-toolbar",<span class:"section-tag","One picture. Many sentences.">
            <button id:"change-picture",type:"button",class:"text-button",'data-action':"pictures","← Choose another picture">>
        ; (recipe_options(st));
        <div class:"picture-workspace",
            <figure class:"scene-detail",(art.scene(scene.art,scene.alt));
                <figcaption <h2 scene.title><p scene.cue>>
                <div class:"scene-prompts",<strong "Look, think, describe."><p "Who can you see? What are they doing? To what? Where? When? How? Why?">
                    <p "Save one sentence, then use the same cards to tell us something else. You can keep adding descriptions.">>
            >
            <section class:"game-board",'aria-label':"Describe this picture",
                <div class:"board-ribbon",<span class:"round-label","Your picture story">; (base.scramble_button(st))>
                ; (base.sentence_zone(st));
                (base.card_area(st));
                <div class:"builder-actions",(base.join_controls(st));
                    <div class:"sentence-actions",
                        <p id:"feedback",class:"feedback",role:"status",if (model.repeated(st)) "That description is already in your book. Try another detail!" else st.notice>
                        <button id:"primary",type:"button",class:"primary-button",'data-action':"primary",
                            *:(if (not model.ready(st)) {disabled:""} else {}),"Save description →">>
                    <button id:"clear-description",type:"button",class:"text-button",'data-action':"clear-description","Clear this sentence">
                >
            >
        >
        <div class:"picture-book",(base.collection(st))>
    >
}
fn picture_page(st) => base.frame(st,3,<span "★ " ++ string(len(st.book)) ++ (if (len(st.book) == 1) " description" else " descriptions")>,<div
    (base.hero("Describe the ","picture!","Use the picture as your inspiration. Build as many different descriptions as you can; your cards never run out."));
    if (st.phase == "picture") picture_picker(st) else picture_play(st);
    <p class:"footer-note","Choose cards that describe what you see. Every card is reusable, including in sentences joined with and, but, or because.">
>)
// whether the sentence is being read aloud is view state, so it joins the level state for rendering only.
pub fn page(app,reading=false) {
    let st = if (app.level == 0) app.play else {*:app.play,reading:reading}
    if (app.level == 0) levels_screen(app) else if (app.level == 1) base.page(st)
    else if (app.level == 2) puzzle_page(st) else picture_page(st)
}
