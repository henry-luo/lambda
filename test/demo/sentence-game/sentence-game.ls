// Run: ./lambda.exe demo sentence-game
import dom
import resources: ~~.mod_resources
import game_model: .mod_game
import scene: .mod_game_scene

let CSS = input(url_resolve(resources.entry_uri()^,"sentence-game.css"),'text')^

view <sentence_app> state game:game_model.new_game() { scene.page(game) }
on click(evt) {
    let button = dom.closest(evt.target,"button")
    let command = dom.get_attribute(button,"data-action")
    if (button == null or command == null) { return 'pass' }
    game = game_model.action(game,command,dom.get_attribute(button,"data-card"))
    return 'handled'
}
on keydown(evt) {
    if (evt.ctrlKey or evt.metaKey or evt.altKey) { return 'pass' }
    let changed = game_model.shortcut(game,lower(string(evt.key)))
    if (changed == game) { return 'pass' }
    game = changed
    return 'prevent-default'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Sentence Match — Lambda Playground"><style CSS>>
    <body apply(<sentence_app>)>
>
