// Run: ./lambda.exe view test/demo/tetris/tetris.ls
import rules: .tetris_core
import dom

let NAMES = ["I", "O", "T", "S", "Z", "J", "L"]
let GAME_MODEL = <tetris_app>
let RESET_GRAVITY = ["down", "drop", "hold", "restart", "pause"]
fn piece_indices(piece) => [for (cell in rules.cells(piece) where cell[1] >= 0)
  cell[1] * 10 + cell[0]]
fn cell_class(value, ghost) => "cell color-" ++ string(value) ++ (if (ghost) " ghost" else "")
fn preview(kind) {
  <div class:"preview", 'aria-label':(if (kind < 0) "Empty hold" else NAMES[kind] ++ " piece"),
    if (kind >= 0) {
      let indices = piece_indices({*:rules.spawn(kind), x: 0})
      for (i in 0 to 15)
        <div class:cell_class(if (contains(indices, (i div 4) * 10 + i % 4)) kind + 1 else 0, false)>
    }
  >
}
fn key_command(key) {
  if (key == "ArrowLeft" or key == "a" or key == "A") "left"
  else if (key == "ArrowRight" or key == "d" or key == "D") "right"
  else if (key == "ArrowDown" or key == "s" or key == "S") "down"
  else if (key == "ArrowUp" or key == "x" or key == "X" or key == "w" or key == "W") "cw"
  else if (key == "z" or key == "Z") "ccw"
  else if (key == " " or key == "Space") "drop"
  else if (key == "c" or key == "C" or key == "Shift") "hold"
  else if (key == "p" or key == "P" or key == "Escape" or key == "Enter") "pause"
  else if (key == "r" or key == "R") "restart"
  else ""
}
fn mode_title(mode) {
  if (mode == "ready") "Ready to stack?"
  else if (mode == "paused") "Take a breather."
  else "Game over."
}

view <tetris_app> state game: rules.new_game(42), falling_ms: 0, auto_plan: null {
  let active = piece_indices(game.piece)
  let ghost = piece_indices(rules.landing(game.board, game.piece));
  <main id:"tetris", class:"game", tabindex:"0", 'data-mode':game.mode, 'data-autoplay':string(game.autoplay),
    'data-x':string(game.piece.x), 'data-y':string(game.piece.y),
    'data-rotation':string(game.piece.rotation), 'data-kind':string(game.piece.kind),
    <header
      <div <p class:"eyebrow", "LAMBDA / PLAYGROUND">
        <h1 "TETRIS" <span class:"title-dot", ".">>>
      <div class:"badge", "PURE LAMBDA">
    >
    <div class:"play-layout",
      <section class:"board-panel",
        <div class:"board-heading", <span "THE STACK">
          <span class:"mode-label", if (game.mode == "playing") (if (game.autoplay) "AUTO PLAY" else "LIVE") else upper(game.mode)>>
        <div class:"board-wrap",
          <div id:"board", class:"board", role:"img", 'aria-label':"Tetris board, 10 columns by 20 rows",
            for (i in 0 to 199) {
              let is_active = contains(active, i)
              let is_ghost = not is_active and game.board[i] == 0 and contains(ghost, i)
              let value = if (is_active or is_ghost) game.piece.kind + 1 else game.board[i];
              <div class:cell_class(value, is_ghost)>
            }
          >
          if (game.mode != "playing") {
            <div class:"overlay",
              <p class:"eyebrow", if (game.mode == "over") "ONE MORE ROUND?" else "MAKE SOME SPACE">
              <h2 mode_title(game.mode)>
              <p if (game.mode == "over") "Final score: " ++ string(game.score)
                else if (game.mode == "ready") "Seven shapes. Endless possibilities."
                else "Your stack will be here.">
              <button id:"overlay-action", class:(if (game.mode == "over") "restart" else "pause"),
                if (game.mode == "over") "Play again [R]"
                else if (game.mode == "ready") "Start game [Enter]" else "Resume [P]">
              if (game.mode == "ready" or game.mode == "over")
                <button id:"auto-play", class:"auto", "Auto Play">
            >
          }
        >
        <p class:"board-footnote", "Ghost blocks show where your piece will land.">
      >
      <aside class:"sidebar",
        <section class:"stats",
          <p class:"eyebrow", "SCORE"> <div id:"score", class:"score", string(game.score)>
          <div class:"stat-row", <div <p class:"eyebrow", "LEVEL"> <strong id:"level", string(game.level)>>
            <div <p class:"eyebrow", "LINES"> <strong id:"lines", string(game.lines)>>>
        >
        <section class:"pieces", <p class:"eyebrow", "NEXT UP">
          for (i in 0 to 2) preview(game.queue[i])>
        <section class:"held", <p class:"eyebrow", "HOLD / C">
          preview(game.held);
          <p class:"hint", if (game.can_hold) "Swap once per piece" else "Available after locking">
        >
        <div class:"menu-buttons",
          <button id:"pause", class:"pause", if (game.mode == "playing") "Pause [P]" else "Start / resume">
          <button id:"restart", class:"restart", "New game [R]">
        >
      >
    >
    <nav class:"controls", 'aria-label':"Game controls",
      <button class:"left", title:"Move left / Left arrow", "←">
      <button class:"right", title:"Move right / Right arrow", "→">
      <button class:"ccw", title:"Rotate counterclockwise / Z", "↶">
      <button class:"cw", title:"Rotate clockwise / Up arrow", "↷">
      <button class:"down", title:"Soft drop / Down arrow", "↓">
      <button class:"drop", title:"Hard drop / Space", "DROP">
      <button class:"hold", title:"Hold / C", "HOLD">
    >
    <footer "← → move · ↑ / X rotate · Z reverse · ↓ soft drop · Space hard drop · C hold">
  >
}
on click(evt) {
  let button = dom.closest(evt.target, "button")
  let command = dom.get_attribute(button, "class")
  if (contains(["left", "right", "down", "cw", "ccw", "drop", "hold", "pause", "restart", "auto"], command)) {
    game = rules.action(game, command)
    if (command != "pause") {
      game = {*:game, autoplay: command == "auto"}
      auto_plan = null
    }
    if (contains(RESET_GRAVITY, command) or command == "auto") { falling_ms = 0 }
  }
}
on gravity(evt) {
  if (game.mode == "playing") {
    falling_ms = falling_ms + 100
    let interval = if (game.autoplay) 200 else rules.gravity_ms(game.level)
    if (falling_ms >= interval) {
      falling_ms = falling_ms - interval
      if (game.autoplay) {
        let next = rules.auto_step(game, auto_plan)
        game = next.game
        auto_plan = next.plan
      } else { game = rules.action(game, "tick") }
    }
  }
}

// The shell never redraws: its clock continues while the game subtree changes.
view <tetris_shell> {
  <body apply(GAME_MODEL);
    <div id:"gravity-clock", class:"gravity-clock", 'aria-hidden':"true">
  >
}
on keydown(evt) {
  let command = key_command(evt.key)
  if (command != "") {
    // Route keys through the same controls, including when the start button disappears.
    let button = dom.query_selector(dom.root_node(evt.target), "button." ++ command)
    dom.dispatch(button, "click")
    return 'prevent-default'
  }
}
on animationiteration(evt) {
  let target = dom.query_selector(dom.root_node(evt.target), "#tetris")
  dom.dispatch(target, "gravity")
}

<html lang:"en",
  <head <meta charset:"UTF-8"> <title "Tetris — Lambda Playground">
    <link rel:"stylesheet", href:"tetris.css">>
  apply(<tetris_shell>)
>
