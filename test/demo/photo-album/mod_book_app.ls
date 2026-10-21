// Album book procedures: open, mount leaves on demand, frame-driven page turns, drags and jumps.
// State: {book, turned, options, scale, mounted, anim, queue, drag}; `ctx` is {lib, dir, dir_url}.
// The turn is sampled here, not by a CSS transition: transitions do not cover transform yet.
// Progress runs on frame timestamps (the document timeline), so replays are deterministic.
import dom
import updates: ~~.mod_publish
import L: .mod_library
import B: .mod_book
import BU: .mod_book_ui

pub let TURN_SECONDS = 0.55
let JUMP_SECONDS = 0.16
// chrome around the book, as sized in photo-album.css (.bk-bar, .bk-bar-spreads, stage padding)
let CHROME_W = 80.0
let CHROME_H = 52.0 + 112.0 + 40.0
let SHADE = 0.45

fn node(owner, id) => dom.get_element_by_id(owner, id)
fn px(v) => string(round(v * 100) / 100) ++ "px"
fn leaf_node(owner, i) => dom.get_element_by_id(owner, "bk-leaf-" ++ string(i))
fn count(bs) => len(bs.book.leaves)

pn fit(owner) {
    let screen = dom.viewport_size(owner)
    let w = (if (screen == null) 1400.0 else screen.width) - CHROME_W
    let h = (if (screen == null) 900.0 else screen.height) - CHROME_H
    return max(0.05, min(w / (2.0 * B.PAGE_W), h / B.PAGE_H))
}

// leaves near the open spread are displayed: the one under each side and the next one to turn
fn near(bs, i) => i >= bs.turned - 2 and i <= bs.turned + 1

// mounts leaves near the spread; mounted leaves stay (SLD3/SLD4 policy), off-spread ones are hidden
pn mount(owner, ctx, bs) {
    let host = node(owner, "bk-leaves")
    let missing = [for (i in max(0, bs.turned - 2) to min(count(bs) - 1, bs.turned + 1) where not contains(bs.mounted, i)) i]
    if (len(missing) > 0) {
        let html = join([for (i in missing) format([BU.leaf(ctx.lib, bs.book, i, bs.scale, bs.options)], 'html')], "")
        dom.append_child(host, dom.parse_fragment(host, html))
    }
    return {*:bs, mounted:[*bs.mounted, *missing]}
}

// turned leaves stack on the left, later ones on top; unturned ones on the right, earlier on top
fn rest_z(bs, i) => if (i < bs.turned) i + 1 else count(bs) - i

pn shade(owner, i, angle) {
    let a = abs(angle)
    let leaf = leaf_node(owner, i)
    updates.style(dom.query_selector(leaf, ".bk-shade-front"), "opacity", string(round(SHADE * min(1.0, a / 90.0) * 100) / 100))
    updates.style(dom.query_selector(leaf, ".bk-shade-back"), "opacity", string(round(SHADE * min(1.0, (180.0 - a) / 90.0) * 100) / 100))
}

pn pose(owner, i, angle, z) {
    let leaf = leaf_node(owner, i)
    if (leaf == null) { return null }
    updates.style(leaf, "transform", "rotateY(" ++ string(round(angle * 100) / 100) ++ "deg)")
    updates.style(leaf, "z-index", string(z))
    updates.style(leaf, "display", "block")
    shade(owner, i, angle)
    return null
}

// leaves at rest: flat left or right, hidden when off the spread
pn settle(owner, bs) {
    for (i in bs.mounted) {
        if (near(bs, i)) { pose(owner, i, if (i < bs.turned) -180.0 else 0.0, rest_z(bs, i)) }
        else { updates.style(leaf_node(owner, i), "display", "none") }
    }
    // page-edge stacks show how much of the book lies on each side
    updates.style(node(owner, "bk-edge-left"), "width", string(min(14.0, bs.turned * 0.5)) ++ "px")
    updates.style(node(owner, "bk-edge-right"), "width", string(min(14.0, (count(bs) - bs.turned) * 0.5)) ++ "px")
    for (b in dom.query_selector_all(owner, "[data-spread]")) updates.attr(b, "aria-current", dom.get_attribute(b, "data-spread") == string(bs.turned))
    updates.text(owner, "bk-position", B.spread_label(bs.book, bs.turned))
    updates.attr(node(owner, "book"), "data-turned", bs.turned)
    updates.attr(node(owner, "book"), "data-closed", bs.turned == 0 or bs.turned == count(bs))
    if (bs.turned == 0) dom.set_attribute(node(owner, "bk-prev"), "disabled", "") else dom.remove_attribute(node(owner, "bk-prev"), "disabled")
    if (bs.turned == count(bs)) dom.set_attribute(node(owner, "bk-next"), "disabled", "") else dom.remove_attribute(node(owner, "bk-next"), "disabled")
    return bs
}

// the stage: book frame, edges and an empty leaf host; leaves mount into it
pn build(owner, ctx, bs) {
    let s = fit(owner)
    let w = 2.0 * B.PAGE_W * s
    let h = B.PAGE_H * s
    updates.markup(node(owner, "bk-stage"), format([<div class:"bk-book", id:"bk-book", style:"width:" ++ px(w) ++ ";height:" ++ px(h) ++ ";",
        <div class:"bk-edge bk-edge-left", id:"bk-edge-left">;
        <div class:"bk-edge bk-edge-right", id:"bk-edge-right">;
        <div class:"bk-leaves", id:"bk-leaves", style:"left:" ++ px(w / 2.0) ++ ";width:" ++ px(w / 2.0) ++ ";height:" ++ px(h) ++ ";">>], 'html'))
    updates.markup(node(owner, "bk-bar"), format([BU.spread_bar(ctx.lib, bs.book)], 'html'))
    // an unchanged frame keeps its element, leaves included: a rebuild starts from no leaves
    dom.set_inner_html(node(owner, "bk-leaves"), "")
    let fresh = mount(owner, ctx, {*:bs, scale:s, mounted:[]})
    return settle(owner, fresh)
}

pub pn start(owner, ctx, ids, title) {
    let bs = {book:B.plan(ctx.lib, ids, title), turned:0, options:{cover:"navy", paper:"cream"}, scale:1.0,
        mounted:[], anim:null, queue:[], drag:null, frame:0}
    updates.text(owner, "bk-title", title)
    dom.set_selected_index(node(owner, "bk-cover"), 0)
    dom.set_selected_index(node(owner, "bk-paper"), 0)
    return build(owner, ctx, bs)
}

// cover or paper changed, or the window resized: rebuild the mounted leaves at the current spread
pub pn restyle(owner, ctx, bs, options) {
    return build(owner, ctx, {*:bs, options:options, anim:null, queue:[], drag:null})
}

// ---------------------------------------------------------------- turns

// starts (or queues) one turn; a turn runs from an angle, so a released drag continues from it
pn begin(owner, ctx, bs, forward, seconds, from) {
    let i = if (forward) bs.turned else bs.turned - 1
    if (i < 0 or i >= count(bs)) { return bs }
    let ready = mount(owner, ctx, {*:bs, turned:if (forward) bs.turned + 1 else bs.turned})
    let shown = {*:ready, turned:bs.turned}
    // the page the leaf uncovers must be displayed beneath it
    for (j in shown.mounted) { if (j >= i - 1 and j <= i + 1) { pose(owner, j, if (j < i or (j == i and not forward)) -180.0 else 0.0, rest_z(shown, j)) } }
    let anim = {leaf:i, forward:forward, from:from, to:if (forward) -180.0 else 0.0, start:null, seconds:seconds}
    pose(owner, i, from, count(bs) + 10)
    return {*:shown, anim:anim, frame:dom.request_frame(owner, "book_frame")}
}

pub pn turn(owner, ctx, bs, forward) {
    if (bs.anim != null or bs.drag != null) { return {*:bs, queue:[*bs.queue, forward]} }
    return begin(owner, ctx, bs, forward, TURN_SECONDS, if (forward) 0.0 else -180.0)
}

// flips leaf by leaf to a spread, quickly
pub pn jump(owner, ctx, bs, target) {
    let to = max(0, min(count(bs), target))
    let pending = bs.turned + len([for (f in bs.queue where f) f]) - len([for (f in bs.queue where not f) f]) +
        (if (bs.anim == null) 0 else if (bs.anim.forward) 1 else -1)
    let steps = to - pending
    let more = [for (k in 1 to abs(steps) where steps != 0) steps > 0]
    let queued = {*:bs, queue:[*bs.queue, *more]}
    if (queued.anim != null or len(queued.queue) == 0) { return queued }
    return begin(owner, ctx, {*:queued, queue:slice(queued.queue, 1, len(queued.queue))}, queued.queue[0], JUMP_SECONDS,
        if (queued.queue[0]) 0.0 else -180.0)
}

// one animation frame at `now_ms`: sample the eased angle; at the end land the leaf and start any queued turn
pub pn frame(owner, ctx, bs, now_ms) {
    let a0 = bs.anim
    if (a0 == null) { return bs }
    // a turn's clock starts at its first frame
    let a = if (a0.start == null) {*:a0, start:now_ms} else a0
    let p = (now_ms - a.start) / (a.seconds * 1000.0)
    let angle = B.turn_angle(a.from, a.to, p)
    pose(owner, a.leaf, angle, count(bs) + 10)
    if (p < 1.0) { return {*:bs, anim:a, frame:dom.request_frame(owner, "book_frame")} }
    // a leaf that lands on the left is turned
    let landed = if (a.to < -90.0) a.leaf + 1 else a.leaf
    let done = settle(owner, mount(owner, ctx, {*:bs, anim:null, frame:0, turned:landed}))
    if (len(done.queue) == 0) { return done }
    let next = done.queue[0]
    return begin(owner, ctx, {*:done, queue:slice(done.queue, 1, len(done.queue))}, next,
        if (len(done.queue) > 1) JUMP_SECONDS else TURN_SECONDS, if (next) 0.0 else -180.0)
}

// ---------------------------------------------------------------- drags

// a press on the right page drags the next leaf, on the left page the previous one
pub pn press(owner, bs, evt) {
    if (bs.anim != null) { return bs }
    let book = dom.bounding_box(node(owner, "bk-book"))
    let forward = evt.clientX >= book.x + book.width / 2.0
    if ((forward and bs.turned >= count(bs)) or (not forward and bs.turned <= 0)) { return bs }
    return {*:bs, drag:{forward:forward, x:evt.clientX, edge:if (forward) book.x + book.width else book.x, width:book.width, moved:false, angle:if (forward) 0.0 else -180.0}}
}

pub pn drag(owner, ctx, bs, evt) {
    let d = bs.drag
    let moved = d.moved or abs(evt.clientX - d.x) > 4.0
    if (not moved) { return bs }
    let leaf = if (d.forward) bs.turned else bs.turned - 1
    let prepared = if (d.moved) bs else (
        let ready = mount(owner, ctx, {*:bs, turned:if (d.forward) bs.turned + 1 else bs.turned}),
        {*:ready, turned:bs.turned})
    if (not d.moved) {
        for (j in prepared.mounted) { if (j >= leaf - 1 and j <= leaf + 1) { pose(owner, j, if (j < leaf or (j == leaf and not d.forward)) -180.0 else 0.0, rest_z(prepared, j)) } }
    }
    // the angle follows the pointer across the spread
    let travelled = if (d.forward) d.edge - evt.clientX else evt.clientX - d.edge
    let angle = B.drag_angle(d.forward, travelled, d.width)
    pose(owner, leaf, angle, count(bs) + 10)
    return {*:prepared, drag:{*:d, moved:true, angle:angle}}
}

// release finishes the turn past the vertical, otherwise it falls back; a press without movement turns
pub pn release(owner, ctx, bs) {
    let d = bs.drag
    let ended = {*:bs, drag:null}
    if (not d.moved) { return turn(owner, ctx, ended, d.forward) }
    let leaf = if (d.forward) bs.turned else bs.turned - 1
    let finish = B.completes(d.forward, d.angle)
    let target = if (finish == d.forward) -180.0 else 0.0
    let anim = {leaf:leaf, forward:d.forward, from:d.angle, to:target, start:null,
        seconds:TURN_SECONDS * max(0.2, abs(target - d.angle) / 180.0)}
    return {*:ended, anim:anim, frame:dom.request_frame(owner, "book_frame")}
}
