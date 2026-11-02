import chat: lambda.chat

pub fn users(argument: string) array^ {
    let ids = [for (part in split(slice(argument, 5), ",")) trim(part)]
    let unknown = [for (id in ids where chat.profile(id) == null) id]
    if (not starts_with(argument, "user=") or len(ids) == 0 or contains(ids, ""))
        raise error("Chat: supply a comma-separated list such as user=maya,alex")
    else if (len(unique(ids)) != len(ids))
        raise error("Chat: each user must appear only once")
    else if (len(unknown) > 0)
        raise error("Chat: unknown user '" ++ unknown[0] ++ "'. Choose " ++ join([for (p in chat.profiles) p.id], ", "))
    else ids
}
pub fn peer(ids, user) => (let others = [for (id in ids where id != user) id],
    if (len(others) > 0) others[0] else [for (p in chat.profiles where p.id != user) p.id][0])
pub fn client_args(options, user) => [for (argument in options) replace(argument, "{user}", user)]
