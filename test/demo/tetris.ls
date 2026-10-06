// Run: ./lambda.exe view test/demo/tetris.ls
import rules: .tetris_core

let NAMES = ["I", "O", "T", "S", "Z", "J", "L"]
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

view <tetris_app> state game: rules.new_game(42) {
  let active = piece_indices(game.piece)
  let ghost = piece_indices(rules.landing(game.board, game.piece));
  // Own the body so keyboard input survives removal of the focused start button.
  <body <main id:"tetris", class:"game", tabindex:"0", 'data-mode':game.mode,
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
          <span class:"mode-label", if (game.mode == "playing") "LIVE" else upper(game.mode)>>
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
    // Animation iterations provide a document-owned clock; no global mutable state.
    <div id:"gravity-clock", class:"gravity-clock", 'aria-hidden':"true",
      style:"animation-duration:" ++ string(rules.gravity_ms(game.level)) ++ "ms;animation-play-state:" ++
        (if (game.mode == "playing") "running" else "paused")>
  >>
}
on keydown(evt) {
  let command = key_command(evt.key)
  if (command != "") {
    game = rules.action(game, command)
    return 'prevent-default'
  }
}
on click(evt) {
  let command = evt.target_class
  if (contains(["left", "right", "down", "cw", "ccw", "drop", "hold", "pause", "restart"], command)) {
    game = rules.action(game, command)
  }
}
on animationiteration(evt) {
  game = rules.action(game, "tick")
}

<html lang:"en",
  <head <meta charset:"UTF-8"> <title "Tetris — Lambda Playground">
    <link rel:"stylesheet", href:"tetris.css">>
  apply(<tetris_app>)
>
