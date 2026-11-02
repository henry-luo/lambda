// S10.1.2v4/S7.7.3: bare callable pipes share ordinary call outcome admission.
fn clean(value: int) int => value + 1
fn failed(value) int^ => error("pipe failure")
fn collected() array => [1, 2] |> failed(~)
fn opaque(value) => value
let retained = 7 |> failed
let deferred = opaque("wrong") |> clean
let selected = failed;
[7 |> clean, retained is error, deferred is error,
 ((7 |> selected) ^ { 99 }), ((7 |> failed) ^ { ^.message }), collected() |> ~ is error]
