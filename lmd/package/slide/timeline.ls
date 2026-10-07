import c: .common
import effects: .effects
import sampler: .sample

fn walk_children(items, targets, begin, path, sequential, index, elapsed, longest, tracks, ordering = []) map^ {
    if (index >= len(items)) {duration_ms: if (sequential) elapsed else longest, tracks: tracks}
    else {
        let child = items[index]
        let child_ordering = if (sequential) [*ordering, {group: path, child: index}] else ordering
        let compiled = walk(child, targets, begin + (if (sequential) elapsed else 0.0), path ++ "." ++ string(index), child_ordering)^
        walk_children(items, targets, begin, path, sequential, index + 1,
            elapsed + compiled.duration_ms, max(longest, compiled.duration_ms), [*tracks, *compiled.tracks], ordering)^
    }
}

fn walk(node, targets, begin, path, ordering) map^ {
    if (not (node is element)) raise c.fail(path, "expected effect/group element")
    else if (name(node) == 'effect') {
        let expanded = effects.expand(node, targets, begin, path)^;
        {*: expanded, tracks: [for (tr in expanded.tracks) {*: tr, ordering: ordering}]}
    }
    else if (name(node) == 'parallel' or name(node) == 'sequence') {
        let checked_1 = c.attributes(node, [], path)^;
        let guard_1 = if (len(content(node)) == 0) raise c.fail(path, "empty effect group")
        walk_children(content(node), targets, begin, path, name(node) == 'sequence', 0, 0.0, 0.0, [], ordering)^
    } else raise c.fail(path, "unsupported effect group")
}

fn sequential(a, b) => any([for (x in a.ordering) for (y in b.ordering) x.group == y.group and x.child != y.child])
fn overlap(a, b) => a.target == b.target and a.channel == b.channel and not sequential(a, b) and
    (if (a.duration_ms == 0.0 or b.duration_ms == 0.0)
        a.begin_ms <= b.begin_ms + b.duration_ms * b.repeat and b.begin_ms <= a.begin_ms + a.duration_ms * a.repeat
     else a.begin_ms < b.begin_ms + b.duration_ms * b.repeat and b.begin_ms < a.begin_ms + a.duration_ms * a.repeat)

fn cue(node, targets, index, path) map^ {
    let checked_2 = c.attributes(node, ["id", "start"], path)^;
    let cue_start = c.value(node.start, 'click')
    let guard_2 = if (not contains(['entry', 'click'], cue_start)) raise c.fail(path, "invalid cue start")
    let guard_3 = if (len(content(node)) == 0) raise c.fail(path, "empty cue")
    let compiled = walk_children(content(node), targets, 0.0, path, false, 0, 0.0, 0.0, [])^
    let tracks = compiled.tracks
    let checked_duration = if (not c.finite(compiled.duration_ms)) raise c.fail(path, "cue duration overflow")
    let checked_id = if (node.id != null and not c.text_id(node.id)) raise c.fail(path, "ID must be nonempty text")
    let conflicts = [for (i, a in tracks) for (j, b in tracks where j > i and overlap(a, b)) a.target ++ "." ++ string(a.channel)]
    let guard_4 = if (len(conflicts) > 0) raise c.fail(path, "parallel writers for " ++ conflicts[0])
    {id: c.node_id(node, path), index: index, start: cue_start, duration_ms: compiled.duration_ms, tracks: tracks}
}

fn prepare_tracks(tracks, index, paints, built) {
    if (index >= len(tracks)) {tracks: built, paints: paints}
    else {
        let tr = tracks[index]
        let prepared = if (tr.mode == 'color' and paints[tr.target] != null) {*: tr, a: paints[tr.target]} else tr
        let next_paints = if (tr.channel == 'paint') {*: paints, *: map([tr.target, tr.b])} else paints
        prepare_tracks(tracks, index + 1, next_paints, [*built, prepared])
    }
}

fn compile_cues(nodes, targets, index, path, paints, built) array^ {
    if (index >= len(nodes)) built
    else {
        let compiled = cue(nodes[index], targets, index, path ++ ".cue" ++ string(index))^
        // Sequential highlights inherit the last typed paint, including preceding cues.
        let ordered = sort([for (i, tr in compiled.tracks) {index: i, track: tr}], (entry) => [entry.track.begin_ms, entry.index])
        let prepared = prepare_tracks([for (entry in ordered) entry.track], 0, paints, [])
        compile_cues(nodes, targets, index + 1, path, prepared.paints, [*built, {*: compiled, tracks: prepared.tracks}])^
    }
}

pub fn compile(scene) map^ {
    let cues = compile_cues(c.children(scene.source, 'cue'), scene.targets, 0, scene.id, {}, [])^
    let checked_3 = c.check_unique(cues, scene.id ++ ".cues")^;
    let bad_entry = [for (i, entry in cues where entry.start == 'entry' and
        any([for (j, earlier in cues where j < i) earlier.start == 'click'])) entry]
    let guard_5 = if (len(bad_entry) > 0) raise c.fail(scene.id, "entry cues must precede click cues")
    let compiled = {*: scene, cues: cues};
    {*: compiled, prepared_targets: [for (obj in scene.targets) sampler.prepare_object(compiled, obj)]}
}
