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
    let checked_2 = c.attributes(node, ["id", "start", "trigger"], path)^;
    let cue_start = c.value(node.start, 'click')
    let guard_2 = if (not contains(['entry', 'click', 'with-previous', 'after-previous'], cue_start)) raise c.fail(path, "invalid cue start")
    let checked_trigger = if (node.trigger != null and (not c.text_id(node.trigger) or
        not any([for (target in targets) target.id == c.as_text(node.trigger) and target.source.id != null])))
        raise c.fail(path,"trigger requires an explicit object ID")
    let guard_3 = if (len(content(node)) == 0) raise c.fail(path, "empty cue")
    let compiled = walk_children(content(node), targets, 0.0, path, false, 0, 0.0, 0.0, [])^
    let tracks = compiled.tracks
    let checked_duration = if (not c.finite(compiled.duration_ms)) raise c.fail(path, "cue duration overflow")
    let checked_id = if (node.id != null and not c.text_id(node.id)) raise c.fail(path, "ID must be nonempty text")
    let conflicts = [for (i, a in tracks) for (j, b in tracks where j > i and overlap(a, b)) a.target ++ "." ++ string(a.channel)]
    let guard_4 = if (len(conflicts) > 0) raise c.fail(path, "parallel writers for " ++ conflicts[0])
    {id: c.node_id(node, path), trigger:if (node.trigger == null) null else c.as_text(node.trigger), aliases:[c.node_id(node,path)], index: index, start: cue_start, duration_ms: compiled.duration_ms, tracks: tracks}
}

fn prepare_tracks(tracks, index, paints, built) {
    if (index >= len(tracks)) {tracks: built, paints: paints}
    else {
        let tr = tracks[index]
        let prepared = if (tr.mode == 'color' and paints[tr.target ++ "." ++ string(tr.channel)] != null) {*: tr, a: paints[tr.target ++ "." ++ string(tr.channel)]} else tr
        let next_paints = if (tr.channel in ['paint','stroke']) {*: paints, *: map([tr.target ++ "." ++ string(tr.channel), tr.b])} else paints
        prepare_tracks(tracks, index + 1, next_paints, [*built, prepared])
    }
}

fn prepare_cues(cues, index, paints, built) array^ {
    if (index >= len(cues)) built
    else {
        let compiled = cues[index]
        // Sequential highlights inherit the last typed paint, including preceding cues.
        let ordered = sort([for (i, tr in compiled.tracks) {index: i, track: tr}], (entry) => [entry.track.begin_ms, entry.index])
        let prepared = prepare_tracks([for (entry in ordered) entry.track], 0, paints, [])
        prepare_cues(cues, index + 1, prepared.paints, [*built, {*: compiled, tracks: prepared.tracks}])^
    }
}

// automatic starts remain in one navigable build; offsets use the previous cue's start/end.
fn schedule(cues,index,built,previous_begin,previous_end) array^ {
    if (index >= len(cues)) built else {
        let current = cues[index]
        let automatic = current.start in ['with-previous','after-previous']
        let prior = built[last]
        let checked_previous = if (automatic and prior == null) raise c.fail(current.id,"automatic start requires a previous cue")
        let offset = if (current.start == 'with-previous') previous_begin else if (automatic) previous_end else 0.0
        let tracks = [for (tr in current.tracks) {*:tr,begin_ms:tr.begin_ms+offset}]
        let conflicts = if (automatic) [for (a in prior.tracks) for (b in tracks where overlap(a,b) and
            not (a.begin_ms+a.duration_ms*a.repeat <= b.begin_ms and current.start == 'after-previous')) a] else []
        let checked_conflicts = if (len(conflicts)>0) raise c.fail(current.id,"automatic builds have overlapping channel writers")
        let merged = if (automatic) {*:prior,aliases:[*prior.aliases,*current.aliases],
            duration_ms:max(prior.duration_ms,offset+current.duration_ms),tracks:[*prior.tracks,*tracks]} else {*:current,index:len(built)};
        schedule(cues,index+1,if (automatic) [*slice(built,0,len(built)-1),merged] else [*built,merged],offset,offset+current.duration_ms)^
    }
}

pub fn compile(scene) map^ {
    let sources = c.children(scene.source,'cue')
    let raw = [for (i,node in sources) cue(node,scene.targets,i,scene.id ++ ".cue" ++ string(i))^]
    let checked_ids = c.check_unique(raw,scene.id ++ ".cues")^
    // one effect expands to several tracks, so validate IDs on source leaves instead.
    let checked_effect_ids = c.check_unique(effect_names(sources),scene.id ++ ".effects")^
    let cues = schedule([for (entry in raw where entry.trigger == null) entry],0,[],0.0,0.0)^
    let scheduled = prepare_cues(cues,0,{},[])^
    let triggers = [for (entry in raw where entry.trigger != null) prepare_cues([entry],0,{},[])^[0]]
    let checked_trigger_start = if (any([for (entry in triggers) entry.start != 'click'])) raise c.fail(scene.id,"trigger cues use click start")
    let bad_entry = [for (i,entry in scheduled where entry.start == 'entry' and
        any([for (j,earlier in scheduled where j<i) earlier.start == 'click'])) entry]
    let guard_5 = if (len(bad_entry)>0) raise c.fail(scene.id,"entry cues must precede click cues")
    let compiled = {*:scene,cues:scheduled,triggers:triggers};
    {*:compiled,prepared_targets:[for (obj in scene.targets) sampler.prepare_object(compiled,obj)]}
}
fn effect_names(nodes) => [for (node in nodes)
    *if (name(node) == 'effect') (if (node.id == null) [] else [{id:string(node.id)}]) else effect_names(content(node))]
