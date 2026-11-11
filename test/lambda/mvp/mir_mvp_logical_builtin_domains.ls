// D2.3.5/S7.9.3: selected builtin values retain their full transport domain.
pub fn command(raw) any^ {
    let text = string(raw or "")
    if (len(text) > 1 and slice(text, 0, 1) == "\\") slice(text, 1, len(text)) else text
}
fn closed(raw: string) any^ => command(raw)^
let values = [1, 2, 3]
let selected = values or null;
[closed("\\alpha")^, command(null)^, len(selected), string(42)]
