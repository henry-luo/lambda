// Pure chat transitions: persistence and socket ownership stay in the service.
pub let PROFILES = [
    {id: "alex", name: "Alex Morgan", initials: "AM", color: "sage", about: "Making things with Lambda"},
    {id: "maya", name: "Maya Chen", initials: "MC", color: "peach", about: "A little more curious every day"},
    {id: "noah", name: "Noah Williams", initials: "NW", color: "lavender", about: "Out exploring"},
    {id: "priya", name: "Priya Sharma", initials: "PS", color: "blue", about: "One idea at a time"}
]
pub fn profile(id) => [for (p in PROFILES where p.id == id) p][0]
pub fn for_user(messages, user) => [for (m in messages where m.from == user or m.to == user) m]
pub fn conversation(messages, user, peer) => [for (m in messages
    where (m.from == user and m.to == peer) or (m.from == peer and m.to == user)) m]
pub fn message_of(messages, user, id) => [for (m in messages where m.from == user and m.id == id) m][0]
pub fn accept(messages, user, request, stamp) {
    let old = message_of(messages, user, request.id)
    let invalid = profile(user) == null or profile(request.to) == null or user == request.to
        or not (request.id is string) or len(request.id) == 0 or len(request.id) > 128
        or not (request.text is string) or len(trim(request.text)) == 0 or len(request.text) > 4000
    if (invalid) {messages: messages, message: null, error: "Choose a contact and write a message of at most 4000 characters."}
    else if (old != null and (old.to != request.to or old.text != trim(request.text)))
        {messages: messages, message: null, error: "That message ID already belongs to another message."}
    else if (old != null) {messages: messages, message: old, error: null}
    else (let made = {id: request.id, seq: len(messages) + 1, from: user, to: request.to,
        text: trim(request.text), stamp: stamp, status: "sent"},
        {messages: [*messages, made], message: made, error: null})
}
pub fn receipt(messages, user, id, from, status) => [for (m in messages)
    if (m.id == id and m.from == from and m.to == user and
        (status == "read" or (status == "delivered" and m.status == "sent"))) {*: m, status: status} else m]
pub fn merge(messages, message) => if (len([for (m in messages where m.id == message.id and m.from == message.from) m]) == 0)
    [*messages, message] else [for (m in messages) if (m.id == message.id and m.from == message.from) message else m]
pub fn unread(messages, user, peer) => len([for (m in messages where m.to == user and m.from == peer and m.status != "read") m])
pub fn preview(messages, user, peer) => (let ms = conversation(messages, user, peer),
    if (len(ms) == 0) "Start a conversation" else ms[len(ms) - 1].text)
