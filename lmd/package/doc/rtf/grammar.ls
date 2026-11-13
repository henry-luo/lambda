// Emit RTF records without recursive groups: skipped destinations can exceed the visible depth limit (D9.3.4).
pub grammar document {
    Document = (Control | Hex | Symbol | Open | Close | Text)*
    Control = _slash, word: word, argument: argument?, delimiter: delimiter?
    Hex = _slash, _quote, value: hexpair
    Symbol = _slash, value: char?
    Open = _open
    Close = _close
    Text = value: text
    word := ("a" to "z" | "A" to "Z")+
    argument := "-" "\d"* | "\d"+
    hexpair := ("\d" | "a" to "f" | "A" to "F"){2}
    char := "\n" | !"\n"
    text := !("{" | "}" | "\\")+
    delimiter := " "
    _slash := "\\"
    _quote := "'"
    _open := "{"
    _close := "}"
}
