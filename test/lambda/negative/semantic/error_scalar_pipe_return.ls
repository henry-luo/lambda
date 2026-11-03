// S10.1.2v4/S7.5.1v6: an open mapping source may apply to one scalar.
fn failed(value) int^ => error("pipe")
fn scalar_mapping(value) => value |> failed(~)
scalar_mapping(7)
