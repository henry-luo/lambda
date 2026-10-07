// Continuous input events reach author templates that declare them (ES5v2):
// `mousemove` and `wheel` (with its deltas) on a pad, `scroll` from a
// scrollable box. A sibling template declares none of them and never runs.

view <motion_pad> state moves: 0, last_x: 0.0, wheel_y: 0.0, wheels: 0 {
  <div id:"pad", class:"pad",
    <output id:"moves", string(moves)>
    <output id:"last-x", string(int(last_x))>
    <output id:"wheel", string(wheels) ++ ":" ++ string(int(wheel_y))>
  >
}
on mousemove(evt) {
  moves = moves + 1
  last_x = evt.x
}
on wheel(evt) {
  wheels = wheels + 1
  wheel_y = wheel_y + evt.deltaY
}

view <scroll_box> state scrolls: 0 {
  <div id:"box", class:"box",
    <output id:"scrolls", string(scrolls)>
    <div class:"tall", "tall content">
  >
}
on scroll(evt) {
  scrolls = scrolls + 1
}

view <quiet_box> state clicks: 0 {
  <div id:"quiet", class:"quiet", <output id:"clicks", string(clicks)>>
}
on click(evt) {
  clicks = clicks + 1
}

<html
  <head
    <style "
      body { margin: 0; font-family: sans-serif; }
      .pad { position: absolute; left: 0; top: 0; width: 400px; height: 200px; background: #eef; }
      .box { position: absolute; left: 0; top: 220px; width: 400px; height: 150px; overflow: auto; background: #efe; }
      .tall { height: 1000px; }
      .quiet { position: absolute; left: 450px; top: 0; width: 200px; height: 200px; background: #fee; }
    ">
  >
  <body
    apply(<motion_pad>)
    apply(<scroll_box>)
    apply(<quiet_box>)
  >
>
