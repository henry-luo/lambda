// S7.7.3/S7.5.5: a dynamic callable's argument contract is checked at runtime.
pn invoke(f: function) { f(); 42 }
pn main() { invoke(() => 7) }

fn failed(value) int^ => error("pipe")
pn pipe_outcome() { 7 |> failed; 42 }
