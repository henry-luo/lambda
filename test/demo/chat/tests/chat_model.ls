import model: lambda.chat.model
let first = model.accept([], "alex", {id: "one", to: "maya", text: "  Hello 世界  "}, "10:20")
let duplicate = model.accept(first.messages, "alex", {id: "one", to: "maya", text: "Hello 世界"}, "10:21")
let collision = model.accept(first.messages, "alex", {id: "one", to: "noah", text: "Another"}, "10:22")
let delivered = model.receipt(first.messages, "maya", "one", "alex", "delivered")
let read = model.receipt(delivered, "maya", "one", "alex", "read")
let regression = model.receipt(read, "maya", "one", "alex", "delivered")
let spoof = model.receipt(first.messages, "noah", "one", "alex", "read");
[len(first.messages), first.message.text, duplicate.messages == first.messages,
 collision.error != null, delivered[0].status, read[0].status, regression[0].status,
 spoof == first.messages, len(model.for_user(first.messages, "noah")),
 model.unread(first.messages, "maya", "alex"), model.unread(read, "maya", "alex"),
 model.accept([], "alex", {id: "bad", to: "alex", text: "Hi"}, "10:20").error != null,
 model.accept([], "alex", {id: "empty", to: "maya", text: " "}, "10:20").error != null]
