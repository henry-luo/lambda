// handler-local ^ retains error metadata through either member spelling (S7.4.4, S8.2.1v4).
fn fail() int^ {
    raise error("handler-current-error")
}

let message = fail() ^ { ^.message }
let indexed_message = fail() ^ { ^["message"] }
let piped = fail() ^ { [1, 2] |> ~ * 2 }
let nested = fail() ^ { fail() ^ { "nested-recovered" } }
let handled_member = fail() ^ { {message: ^.message} }.message;
[message, indexed_message, piped, nested, handled_member]
