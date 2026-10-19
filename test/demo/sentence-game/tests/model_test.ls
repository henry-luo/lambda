import game: ~~.mod_model

// enter the builder through the pattern picker before exercising its transitions.
fn begin_builder(seed=42,id="pair") => game.choose_pattern(game.new_game(seed),id)
fn switch(st,id) => game.choose_pattern(game.show_patterns(st),id)
fn advance(st) => game.choose_pattern(game.next(st),st.pattern)

fn classic(st) {
    let subject = st.hands.subject[0]
    let predicate = [for (card in st.hands.predicate where card.match == subject.id) card][0]
    let pair = game.choose(game.choose(st,"subject",subject.id),"predicate",predicate.id)
    if (game.has_object(st.pattern)) game.choose(pair,"object",predicate.object) else pair
}
fn pick(st,s,p,o) {
    let pair = game.choose(game.choose(st,"subject",s.id),"predicate",p.id)
    if (game.has_object(st.pattern)) game.choose(pair,"object",o.id) else pair
}
fn pick_new(st) {
    let choices = [for (s in st.hands.subject,p in st.hands.predicate,o in (if (game.has_object(st.pattern)) st.hands.object else [null])) pick(st,s,p,o)];
    [for (choice in choices where not game.repeated(choice)) choice][0]
}

let initial = begin_builder(42)
let picked = classic(initial)
let saved = game.save_sentence(picked)
let advanced = advance(saved)
{
    catalog:game.card_count == 108 and len(game.subjects) == 22 and len(game.predicates) == 22 and len(game.objects) == 22 and len(game.places) == 16 and len(game.times) == 16 and len(game.manners) == 5 and len(game.reasons) == 5,
    hand_size:all([for (kind in game.kinds(initial.pattern)) len(initial.hands[kind]) == 3]),
    unique_cards:all([for (kind in game.kinds(initial.pattern)) len(unique([for (card in initial.hands[kind]) card.id])) == 3]),
    seeded_deal:initial == begin_builder(42),
    natural_match:all([for (seed in 0 to 20) game.ready(classic(begin_builder(seed)))]),
    incomplete:game.save_sentence(initial) == initial,
    no_skip:game.next(initial) == initial,
    invalid_kind:game.choose(initial,"missing","hen") == initial,
    invalid_card:game.choose(initial,"subject","missing") == initial,
    selected:game.ready(picked),
    sentence:game.sentence(picked) == game.card(picked,"subject").text ++ " " ++ game.card(picked,"predicate").text ++ ".",
    saved:saved.phase == "saved" and len(saved.book) == 1 and saved.book[0].natural,
    locked_save:game.save_sentence(saved) == saved,
    locked_cards:game.choose(saved,"subject",saved.hands.subject[1].id) == saved,
    next_round:game.next(saved).phase == "pattern" and advanced.phase == "build" and advanced.round == 2 and advanced.book == saved.book,
    clears_choices:advanced.subject == "" and advanced.predicate == "",
    immutable_snapshot:len(initial.book) == 0 and initial.subject == ""
}

let repeated_hand = {*:advanced,hands:initial.hands}
let repeated = game.choose(game.choose(repeated_hand,"subject",picked.subject),"predicate",picked.predicate)
let done = reduce([initial,*[for (i in 1 to game.rounds) i]],(st,unused)=>advance(game.save_sentence(pick_new(st))))
let reset = game.action(done,"reset")
{
    duplicate_detected:game.repeated(repeated),
    duplicate_blocked:not game.ready(repeated) and game.save_sentence(repeated) == repeated,
    duplicate_message:contains(game.feedback(repeated),"already"),
    five_sentences:len(done.book) == game.rounds and done.phase == "done",
    distinct_sentences:len(unique([for (entry in done.book) entry.text])) == game.rounds,
    done_locked:game.action(done,"primary") == done,
    reset_empty:reset.phase == "pattern" and reset.round == 1 and len(reset.book) == 0,
    reset_new_deal:reset.seed != initial.seed,
    prior_book_kept:len(done.book) == game.rounds
}

let three = begin_builder(42,"triple")
let triple = classic(three)
let missing_object = game.choose(game.choose(three,"subject",triple.subject),"predicate",triple.predicate)
let triple_saved = game.save_sentence(triple)
let three_done = reduce([three,*[for (i in 1 to game.rounds) i]],(st,unused)=>advance(game.save_sentence(pick_new(st))))
let cross_pattern = classic({*:three,book:saved.book})
{
    three_piles:len(game.kinds(three.pattern)) == 3 and all([for (kind in game.kinds(three.pattern)) len(three.hands[kind]) == 3]),
    three_familiar:all([for (seed in 0 to 20) game.save_sentence(classic(begin_builder(seed,"triple"))).book[0].natural]),
    object_required:not game.complete(missing_object) and game.save_sentence(missing_object) == missing_object,
    object_completes:game.ready(triple) and triple_saved.book[0].clauses[0].object == triple.object,
    three_part_text:game.sentence(triple) == game.card(triple,"subject").text ++ " " ++ game.card(triple,"predicate").text ++ " " ++ game.card(triple,"object").text ++ ".",
    short_verb:game.card(triple,"predicate").text == game.card(triple,"predicate").verb,
    no_hidden_object:game.shortcut(initial,"z") == initial,
    object_shortcut:game.shortcut(three,"z").object == three.hands.object[0].id,
    pattern_switch:switch(triple,"pair").pattern == "pair" and switch(triple,"pair").object == "",
    invalid_pattern:game.choose_pattern(game.show_patterns(triple),"missing") == game.show_patterns(triple),
    saved_pattern_locked:game.choose_pattern(triple_saved,"pair") == triple_saved,
    cross_pattern_duplicate:game.sentence(cross_pattern) == saved.book[0].text and game.repeated(cross_pattern),
    three_book:three_done.phase == "done" and len(three_done.book) == game.rounds,
    reset_to_picker:game.action(three_done,"reset").phase == "pattern" and game.action(three_done,"reset").pattern == ""
}

let joined = game.extend(picked,"and")
let joined_ready = classic(joined)
let mixed = classic(switch(joined,"triple"))
let mixed_saved = game.save_sentence(mixed)
let after_mixed = advance(mixed_saved)
let long_story = reduce([picked,*[for (i in 1 to 7) i]],(st,unused)=>classic(game.extend(st,"and")))
{
    incomplete_no_join:game.extend(initial,"and") == initial,
    invalid_join:game.extend(picked,"nonsense") == picked,
    join_stores_clause:len(joined.clauses) == 1 and joined.clauses[0] == game.current_clause(picked),
    join_requires_new_clause:not game.complete(joined) and game.save_sentence(joined) == joined,
    joined_ready:game.ready(joined_ready) and contains(game.sentence(joined_ready),", and "),
    all_connectors:all([for (connector in game.connectors) contains(game.sentence(classic(game.extend(picked,connector.id))),connector.separator)]),
    because_punctuation:contains(game.sentence(classic(game.extend(picked,"because")))," because ") and not contains(game.sentence(classic(game.extend(picked,"because"))),", because"),
    embedded_article:game.part_text({pattern:"pair",subject:"hen",predicate:"eggs",object:"",link:"and"},"subject") == "the hen",
    proper_name:game.part_text({pattern:"pair",subject:"grandma",predicate:"eggs",object:"",link:"but"},"subject") == "Grandma",
    undo_restores_snapshot:game.current_clause(game.action(joined_ready,"undo-join")) == game.current_clause(picked) and game.action(joined_ready,"undo-join").hands == picked.hands,
    undo_empty:game.action(initial,"undo-join") == initial,
    mixed_patterns:mixed_saved.book[0].clauses[0].pattern == "pair" and mixed_saved.book[0].clauses[1].pattern == "triple",
    mixed_saved:len(mixed_saved.book) == 1 and len(mixed_saved.book[0].clauses) == 2,
    joined_feedback:contains(game.feedback(mixed_saved),"conjunctions"),
    next_clears_chain:len(after_mixed.clauses) == 0 and after_mixed.link == "" and after_mixed.previous == null and after_mixed.pattern == "triple",
    long_chain:len(long_story.clauses) == 7 and len(game.save_sentence(long_story).book[0].clauses) == 8,
    undo_one_join:len(game.action(long_story,"undo-join").clauses) == 6,
    original_choices_kept:picked.subject == initial.hands.subject[0].id and len(picked.clauses) == 0
}
