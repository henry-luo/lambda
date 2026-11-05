import dom

view <probe> state hidden: 0, value: 0, held: null {
    <div id: "owner",
        <button id: "noop", "No change">
        <button id: "change", "Change">
        <output id: "value", string(value)>
        <section id: "row", class: if (value % 2 == 1) "wide" else "narrow",
            <input id: "kept", value: "source">
        >
    >
}
on click(evt) {
    let owner = dom.closest(evt.target, "#owner")
    let action = dom.get_attribute(evt.target, "id")
    if (action == "noop") {
        if (held == null) { held = owner }
        hidden = hidden + 1
        // an equal render must retain imperative edits and the live node identity.
        dom.set_attribute(owner, "data-kept", "yes")
        dom.set_attribute(owner, "data-same", if (dom.same_node(held, owner)) "true" else "false")
    } else if (action == "change") {
        value = value + 1
    }
    return 'handled'
}

<html <head <style ".narrow{width:100px;height:30px}.wide{width:150px;height:40px}">> <body apply(<probe>)>>
