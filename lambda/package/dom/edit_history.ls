// Package-owned UA editing history (D7.2.5). Text entries retain a node
// wrapper plus changed text; structural entries retain only an opaque native
// delta id. Neither form serializes host HTML. The document session root owns
// package data (D5.3.3), and a generic transaction restores it on rollback.
import dom
import session: lambda.dom.edit_session

let history_limit = 128

fn history_take(items, count) {
    if (count <= 0 or len(items) == 0) []
    else [items[0], *history_take(slice(items, 1, len(items)), count - 1)]
}

fn utf16_width(character) {
    if (ord(character) > 65535) 2 else 1
}

fn codepoint_from_utf16(text, target, index, units) {
    if (index >= len(text) or units >= target) index
    else codepoint_from_utf16(text, target, index + 1,
                               units + utf16_width(slice(text, index, index + 1)))
}

fn utf16_from_codepoint(text, target, index, units) {
    if (index >= len(text) or index >= target) units
    else utf16_from_codepoint(text, target, index + 1,
                              units + utf16_width(slice(text, index, index + 1)))
}

fn common_prefix(before, after, index, limit) {
    if (index >= limit or slice(before, index, index + 1) != slice(after, index, index + 1))
        index
    else common_prefix(before, after, index + 1, limit)
}

fn common_suffix(before, after, count, limit) {
    if (count >= limit or
        slice(before, len(before) - count - 1, len(before) - count) !=
        slice(after, len(after) - count - 1, len(after) - count)) count
    else common_suffix(before, after, count + 1, limit)
}

fn delta(node, before, after, selection_before, history_class,
         typing_before, typing_after) {
    if (before == after) null
    else {
        let prefix = common_prefix(before, after, 0, min(len(before), len(after)));
        let suffix = common_suffix(before, after, 0,
                                   min(len(before), len(after)) - prefix);
        {
            node: node,
            start: prefix,
            removed: slice(before, prefix, len(before) - suffix),
            inserted: slice(after, prefix, len(after) - suffix),
            selection_before: selection_before,
            selection_after: {
                anchor: prefix + len(slice(after, prefix, len(after) - suffix)),
                focus: prefix + len(slice(after, prefix, len(after) - suffix))
            },
            history_class: history_class,
            typing_before: typing_before,
            typing_after: typing_after
        }
    }
}

// Text keeps a compact package delta. Every other mutating family records an
// opaque generic DOM delta instead; neither path serializes host HTML.
pub fn capture(host, edit_context, descriptor) {
    let node = if (edit_context.token == null) null
               else dom.edit_node(host, edit_context.token);
    let selection = if (node == null) null
                    else dom.document_selection(dom.owner_document(host));
    let bounds = if (selection == null) null else dom.selection_boundaries(selection);
    let current = session.read(host);
    let typing_before = if (current == null) null else current.typing_state;
    let text_recordable = descriptor.history_class == "typing" or
                          descriptor.history_class == "composition";
    if (text_recordable and node != null and bounds != null and
        bounds.anchor_node != null and bounds.focus_node != null and
        dom.same_node(bounds.anchor_node, node) and
        dom.same_node(bounds.focus_node, node)) {
        let before = dom.node_value(node);
        if (before == null) null else {
            node: node,
            parent: dom.parent_node(node),
            before: before,
            selection_before: {
                anchor: codepoint_from_utf16(before, bounds.anchor_offset, 0, 0),
                focus: codepoint_from_utf16(before, bounds.focus_offset, 0, 0)
            },
            history_class: descriptor.history_class,
            typing_before: typing_before
        }
    }
    else if (descriptor.history_class != "none" and edit_context.token != null) {
        {
            retained: true,
            token: edit_context.token,
            history_class: descriptor.history_class,
            typing_before: typing_before
        }
    }
    else null
}

fn release_entry(host, entry) {
    if (entry == null or entry.delta_id == null) true
    else dom.edit_release_delta(host, entry.delta_id)
}

fn release_entries(host, entries, index) {
    if (entries == null or index >= len(entries)) true
    else release_entry(host, entries[index]) and
         release_entries(host, entries, index + 1)
}

fn history_trim(entries) {
    if (len(entries) <= history_limit) { kept: entries, discarded: [] }
    else {
        let dropped = len(entries) - history_limit;
        { kept: slice(entries, dropped, len(entries)),
          discarded: history_take(entries, dropped) }
    }
}

fn can_merge_typing(previous, current) {
    previous != null and current != null and
    previous.history_class == "typing" and current.history_class == "typing" and
    previous.removed == "" and current.removed == "" and
    dom.same_node(previous.node, current.node) and
    previous.start + len(previous.inserted) == current.start and
    previous.selection_after.anchor == current.start and
    previous.selection_after.focus == current.start
}

fn merge_typing(previous, current) {
    {
        *: previous,
        inserted: previous.inserted ++ current.inserted,
        selection_after: current.selection_after,
        typing_after: current.typing_after
    }
}

// The discarded handles are released only after the new immutable session has
// been installed, so an enclosing transaction can still restore its old root.
fn record_stack(history, entry) {
    let undo_entries = history.undo;
    let count = len(undo_entries);
    let previous = if (count == 0) null else undo_entries[count - 1];
    let unbounded = if (can_merge_typing(previous, entry) == true)
        [*history_take(undo_entries, count - 1), merge_typing(previous, entry)]
    else [*undo_entries, entry];
    let trimmed = history_trim(unbounded);
    {
        history: {
            undo: trimmed.kept,
            redo: [],
            open_group: if (entry.history_class == "typing") "typing" else null
        },
        released: [*history.redo, *trimmed.discarded]
    }
}

// Capture is before package mutation, record is after it. Generic native
// retention preserves structural identity; the package still chooses its
// history class, coalescing boundary, and stack ownership (D7.2.5).
pub pn record(host, snapshot) {
    if (snapshot == null) false
    else {
        let current = session.read(host);
        let typing_after = if (current == null) null else current.typing_state;
        let retained_id = if (snapshot.retained == true)
            dom.edit_retain_delta(host, snapshot.token)
        else null;
        let entry = if (snapshot.retained == true) {
            if (retained_id == null) null else {
                delta_id: retained_id,
                history_class: snapshot.history_class,
                typing_before: snapshot.typing_before,
                typing_after: typing_after
            }
        }
        else {
            let after = dom.node_value(snapshot.node);
            if (after == null) null else delta(snapshot.node, snapshot.before,
                after, snapshot.selection_before, snapshot.history_class,
                snapshot.typing_before, typing_after)
        };
        let same_parent = snapshot.retained == true or
                          (snapshot.parent != null and
                           dom.parent_node(snapshot.node) != null and
                           dom.same_node(snapshot.parent,
                                         dom.parent_node(snapshot.node)));
        let stacked = if (entry == null or current == null or not same_parent)
            null
        else record_stack(current.history, entry);
        let updated = if (stacked == null) null
            else session.set_history(host, stacked.history);
        if (updated == null) {
            release_entry(host, entry);
            false
        }
        else release_entries(host, stacked.released, 0)
    }
}

pub fn can_undo(host) {
    let current = session.read(host);
    current != null and len(current.history.undo) > 0
}

pub fn can_redo(host) {
    let current = session.read(host);
    current != null and len(current.history.redo) > 0
}

fn value_after_replay(value, entry, is_undo) {
    let expected = if (is_undo) entry.inserted else entry.removed;
    let replacement = if (is_undo) entry.removed else entry.inserted;
    let at = entry.start;
    if (at < 0 or at + len(expected) > len(value) or
        slice(value, at, at + len(expected)) != expected) null
    else slice(value, 0, at) ++ replacement ++
         slice(value, at + len(expected), len(value))
}

fn restore_selection(host, entry, is_undo) {
    let selection = dom.document_selection(dom.owner_document(host));
    let point = if (is_undo) entry.selection_before else entry.selection_after;
    let value = dom.node_value(entry.node);
    if (selection == null or value == null) false
    else {
        // Selection APIs use UTF-16 offsets while all package text policy uses
        // codepoints. Convert at this one explicit boundary (D7.2.5).
        dom.set_base_and_extent(selection, entry.node,
                                utf16_from_codepoint(value, point.anchor, 0, 0),
                                entry.node,
                                utf16_from_codepoint(value, point.focus, 0, 0));
        true
    }
}

fn move_entry(history, entry, is_undo) {
    let from = if (is_undo) history.undo else history.redo;
    let to = if (is_undo) history.redo else history.undo;
    // An entry always describes its original forward delta. Undo moves it to
    // redo unchanged; redo replays that same delta and moves it back.
    if (is_undo) { undo: history_take(from, len(from) - 1), redo: [*to, entry], open_group: null }
    else { undo: [*to, entry], redo: history_take(from, len(from) - 1), open_group: null }
}

fn clear_history(host, current) {
    let cleared = { undo: [], redo: [], open_group: null };
    let updated = session.set_history(host, cleared);
    if (updated == null) false
    else release_entries(host, [*current.history.undo, *current.history.redo], 0)
}

fn replay_retained(host, edit_context, current, entry, is_undo) {
    if (not dom.edit_replay_delta(host, edit_context.token, entry.delta_id,
                                  is_undo)) {
        clear_history(host, current);
        false
    }
    else {
        let restored_typing = if (is_undo) entry.typing_before
                              else entry.typing_after;
        let updated = session.set_history_and_typing(host,
            move_entry(current.history, entry, is_undo), restored_typing);
        if (updated == null) { dom.edit_abort_transaction(host, edit_context.token); false }
        else true
    }
}

// Undo/redo itself is a generic transaction. Divergent authored changes clear
// retained state rather than applying an inverse over a different DOM tree.
pn replay(host, edit_context, is_undo) {
    let current = session.read(host);
    let entries = if (current == null) [] else if (is_undo) current.history.undo
                  else current.history.redo;
    if (current == null or edit_context.token == null or len(entries) == 0) false
    else {
        let entry = entries[len(entries) - 1];
        if (entry.delta_id != null)
            replay_retained(host, edit_context, current, entry, is_undo)
        else {
            let value = dom.node_value(entry.node);
            let replayed = if (value == null) null
                           else value_after_replay(value, entry, is_undo);
            if (replayed == null) {
                clear_history(host, current);
                false
            }
            else if (not dom.edit_begin_transaction(host, edit_context.token)) false
            else {
                dom.set_node_value(entry.node, replayed);
                let restored = restore_selection(host, entry, is_undo);
                let restored_typing = if (is_undo) entry.typing_before
                                      else entry.typing_after;
                let updated = if (restored)
                    session.set_history_and_typing(host,
                        move_entry(current.history, entry, is_undo),
                        restored_typing)
                else null;
                if (updated == null) { dom.edit_abort_transaction(host, edit_context.token); false }
                else true
            }
        }
    }
}

pub pn run_undo(host, edit_context) { replay(host, edit_context, true) }
pub pn run_redo(host, edit_context) { replay(host, edit_context, false) }
