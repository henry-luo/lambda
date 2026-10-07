// Run: ./lambda.exe view test/demo/superlambda/superlambda.ls
import world: .superlambda_core
import dom

let MODEL = <superlambda_game>
fn position(x, y, w, h) => "left:" ++ string(x) ++ "px;top:" ++ string(y) ++
  "px;width:" ++ string(w) ++ "px;height:" ++ string(h) ++ "px;"
fn in_camera(item, camera) => item.x + item.w >= camera - 80 and item.x <= camera + world.VIEW_WIDTH + 80
fn sprite(item, camera, kind) =>
  <div class:("object " ++ kind), style:position(item.x - camera, item.y, item.w, item.h),
    if (kind != "ground" and kind != "ledge")
      <img src:("assets/" ++ kind ++ ".svg"), alt:"", draggable:"false">
  >
fn key_command(key) {
  if (key == "ArrowLeft" or key == "a" or key == "A") "left"
  else if (key == "ArrowRight" or key == "d" or key == "D") "right"
  else if (key == " " or key == "Space" or key == "ArrowUp" or key == "w" or key == "W") "jump"
  else if (key == "Shift") "run"
  else if (key == "Enter" or key == "p" or key == "P" or key == "Escape") "pause"
  else if (key == "r" or key == "R") "restart"
  else ""
}
fn control(command, caption, hint) => <button class:("control-" ++ command),
  'data-command':command, title:hint, 'aria-label':hint, caption>
fn event_command(evt) {
  // Physical clicks can target the button's text; synthetic keys target its element.
  dom.get_attribute(evt.target, "data-command") or
    dom.get_attribute(dom.parent_element(evt.target), "data-command")
}
fn overlay_title(mode) {
  if (mode == "ready") "A little hero. A big adventure."
  else if (mode == "paused") "Rest those jumping boots."
  else if (mode == "won") "That's a super finish!"
  else "Every hero gets another try."
}

view <superlambda_game> state game: world.new_game() {
  let h = game.hero
  let camera = game.camera;
  <main id:"superlambda", tabindex:"0", 'data-mode':game.mode,
    'data-x':string(round(h.x)), 'data-y':string(round(h.y)),
    'data-grounded':string(h.grounded), 'data-checkpoint':string(game.checkpoint),
    'data-ticks':string(game.ticks),
    'data-moving':string(contains(game.keys, "right") or contains(game.keys, "left")),
    <section class:"stage", 'aria-label':"Scrolling platform game: Meadow Circuit",
      <img class:"scenery", src:"assets/scenery.svg", alt:"">
      <div class:"world", 'aria-hidden':"true",
        for (s in world.TERRAIN where in_camera(s, camera)) sprite(s, camera, s.kind);
        for (b in world.BLOCKS where in_camera(b, camera))
          sprite(b, camera, if (contains(game.used, b.id)) "used" else b.kind);
        for (c in world.COINS where in_camera(c, camera) and not contains(game.collected, c.id))
          sprite(c, camera, "coin");
        for (e in game.enemies where e.alive and in_camera(e, camera)) sprite(e, camera, e.kind);
        <div class:("checkpoint " ++ (if (game.checkpoint) "activated" else "")),
          style:position(world.CHECKPOINT_X - camera, 300, 44, 100),
          <div class:"flag-pole"> <div class:"checkpoint-flag", "λ">
        >
        <div class:"goal", style:position(world.GOAL_X - camera, 190, 110, 210),
          <div class:"goal-pole"> <div class:"goal-ball"> <div class:"goal-flag", "λ">
          <div class:"goal-base"> <div class:"goal-label", "FINISH">
        >
        <div id:"hero", class:("hero" ++ (if (h.facing < 0) " facing-left" else "") ++
          (if (game.invincible > 0 and game.ticks % 4 < 2) " invincible" else "")),
          style:position(h.x - camera - 6, h.y - 14, 40, 54),
          <img src:"assets/hero.svg", style:("left:" ++ string(-40 *
            (if (not h.grounded) 2 else if (abs(h.vx) > 0.2 and game.ticks % 6 < 3) 1 else 0)) ++ "px;"),
            alt:"Lambda, wearing a red hat with λ", draggable:"false">
        >
      >
      <div class:"hud",
        <div class:"hud-world", <span class:"hud-label", "WORLD 01"> <strong "MEADOW CIRCUIT">>
        <div class:"hud-stat", <span class:"hud-label", "SCORE"> <strong id:"score", string(game.score)>>
        <div class:"hud-stat", <span class:"hud-label", "COINS"> <strong id:"coins", "● " ++ string(game.coins)>>
        <div class:"hud-stat", <span class:"hud-label", "LIVES"> <strong id:"lives", string(game.lives) ++ " / 3">>
        <div class:"hud-stat", <span class:"hud-label", "TIME"> <strong id:"time", string(world.time_left(game))>>
      >
      if (game.mode != "playing") {
        <div class:"veil",
          <section class:"overlay",
            <div class:"overlay-mark", "λ">
            <p class:"eyebrow", if (game.mode == "ready") "WELCOME TO THE MEADOW"
              else if (game.mode == "won") "WORLD 01 / COMPLETE" else upper(game.mode)>
            <h2 overlay_title(game.mode)>
            <p class:"overlay-description", if (game.mode == "ready") "Chase coins. Hop on grubs. Find your way to the flag."
              else if (game.mode == "won") "You made it! Score: " ++ string(game.score) ++ " · Coins: " ++ string(game.coins)
              else if (game.mode == "paused") "The meadow can wait. Come back when you're ready."
              else "Watch the gaps, and jump on enemies from above.">
            <button id:"overlay-action", 'data-command':(if (game.mode == "over" or game.mode == "won") "restart" else "pause"),
              if (game.mode == "ready") "LET'S GO  →" else if (game.mode == "paused") "KEEP GOING  →" else "PLAY AGAIN  →">
            <p class:"overlay-keys", "← → move   ·   Space jump   ·   Shift run">
          >
        >
      }
      <div class:"stage-caption", if (game.checkpoint) "CHECKPOINT SAVED / KEEP GOING →" else "COINS ABOVE. ADVENTURE AHEAD. →">
    >
    <div class:"dashboard",
      <nav class:"movement", 'aria-label':"Hold buttons to move and jump",
        control("left", "←", "Hold to move left / A")
        control("right", "→", "Hold to move right / D")
        control("jump", "JUMP ↑", "Hold for a higher jump / Space")
        control("run", "RUN", "Hold to run faster / Shift")
      >
      <div class:"game-menu",
        control("pause", if (game.mode == "playing") "Ⅱ PAUSE" else "▷ START / RESUME", "Start or pause / Enter or P")
        control("restart", "↻ RESTART", "Restart / R")
      >
    >
    <div class:"progress", 'aria-label':("Level progress: " ++ string(round(h.x / world.GOAL_X * 100)) ++ " percent"),
      <div class:"progress-fill", style:("width:" ++ string(min(100, h.x / world.GOAL_X * 100)) ++ "%;")>>
    <footer <span "TIP / Tap jump for a short hop. Hold it to reach higher.">
      <span if (game.checkpoint) "CHECKPOINT ✓" else "THREE LIVES. ONE GREAT ADVENTURE.">>
  >
}
on press(evt) { game = world.set_control(game, event_command(evt), true) }
on release(evt) { game = world.set_control(game, event_command(evt), false) }
on releasecontrols(evt) {
  // Releasing a menu click must preserve its target until click is delivered.
  if (game.pointer) { game = world.release_controls(game) }
}
on click(evt) {
  let command = event_command(evt)
  if (command == "pause" or command == "restart") { game = world.action(game, command) }
}
on mousedown(evt) {
  let command = event_command(evt)
  if (contains(["left", "right", "jump", "run"], command)) {
    game = {*:world.set_control(game, command, true), pointer: true}
    return 'prevent-default'
  }
}
on frame(evt) { if (game.mode == "playing") { game = world.tick(game) } }

// Keep the clock and keyboard receiver outside the changing world subtree.
view <superlambda_shell> {
  <body
    <div class:"page",
      <header class:"masthead",
        <div class:"brand", <span class:"brand-mark", "λ"> <div <p class:"eyebrow", "LAMBDA / ARCADE">
          <h1 "SUPER" <span "LAMBDA">>>>
        <div class:"edition", <span class:"edition-dot"> "01 / THE MEADOW ADVENTURE">
      >
      apply(MODEL);
      <p class:"colophon", "A LITTLE RETRO. A LITTLE SMOOTHER. ALL LAMBDA.">
    >
    <div id:"frame-clock", 'aria-hidden':"true">
  >
}
on keydown(evt) {
  let command = key_command(evt.key)
  if (command != "") {
    let button = dom.query_selector(dom.root_node(evt.target), ".control-" ++ command)
    dom.dispatch(button, "press")
    return 'prevent-default'
  }
}
on keyup(evt) {
  let command = key_command(evt.key)
  if (command != "") {
    let button = dom.query_selector(dom.root_node(evt.target), ".control-" ++ command)
    dom.dispatch(button, "release")
    return 'prevent-default'
  }
}
on mouseup(evt) {
  dom.dispatch(dom.query_selector(dom.root_node(evt.target), "#superlambda"), "releasecontrols")
}
on animationiteration(evt) {
  dom.dispatch(dom.query_selector(dom.root_node(evt.target), "#superlambda"), "frame")
}

<html lang:"en", <head <meta charset:"UTF-8"> <title "Superlambda — The Meadow Adventure">
  <link rel:"stylesheet", href:"superlambda.css">>
  apply(<superlambda_shell>)
>
