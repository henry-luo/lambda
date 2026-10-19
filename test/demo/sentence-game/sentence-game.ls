// Run: ./lambda.exe demo sentence-game
import dom
import resources: ~~.mod_resources
import model: .mod_model
import scene: .mod_scene

let CSS = input(url_resolve(resources.entry_uri()^,"sentence-game.css"),'text')^

view <sentence_app> state game:model.new_game() { scene.page(game) }
on click(evt) {
    let button = dom.closest(evt.target,"button")
    let command = dom.get_attribute(button,"data-action")
    if (button == null or command == null) { return 'pass' }
    game = model.action(game,command,dom.get_attribute(button,"data-card"))
    return 'handled'
}
on keydown(evt) {
    if (evt.ctrlKey or evt.metaKey or evt.altKey or game.phase != "build") { return 'pass' }
    let changed = model.shortcut(game,lower(string(evt.key)))
    if (changed == game) { return 'pass' }
    game = changed
    return 'prevent-default'
}

<html lang:"en",
    <head <meta charset:"UTF-8"><title "Build a Sentence! — Lambda Playground"><style CSS>>
    <body apply(<sentence_app>)>
>
