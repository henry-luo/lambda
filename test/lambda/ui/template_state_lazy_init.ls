// State defaults run once per instance, including defaults whose stored value is null.
fn initial_value(changed) int^ {
    if (changed) raise error("view state initializer ran after initialization") else 40
}
fn initial_null(changed) null^ {
    if (changed) raise error("null state was mistaken for missing state") else null
}
view <lazy_counter> state changed: false, count: (initial_value(changed)^), empty: (initial_null(changed)^) {
    <button id:~.id, string(count) ++ ":" ++ string(empty)>
}
on click() {
    changed = true
    count = count + 1
}
<html
    <head <title "Lazy template state">>
    <body
        apply(<lazy_counter id:"first">);
        apply(<lazy_counter id:"second">)
    >
>
