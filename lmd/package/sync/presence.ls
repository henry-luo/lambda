// receiver-local leases; presence is ephemeral and never changes document causality.
import v: .schema
import c: .core
import position: .position

pub fn update(entries, replica, actor, counter, selection, ttl, now) any^ {
    if (not v.actor(actor)^ or not v.natural(counter)^ or counter == 0 or
        not v.natural(now)^ or not v.natural(ttl)^ or ttl == 0 or ttl > 120000 or
        not (selection is array) or len(selection) != 2) raise error("sync presence: invalid lease")
    position.resolve(replica,selection[0])^
    position.resolve(replica,selection[1])^
    let previous = c.lookup(entries,actor)^
    if (previous != null and previous.counter >= counter) { return entries }
    return sort([*[for (entry in entries where entry.id != actor) entry],
        {id:actor,counter:counter,expires:now+ttl,selection:selection}],(entry)=>entry.id)^
}

pub fn live(entries, now) => [for (entry in entries where entry.expires > now) entry]
