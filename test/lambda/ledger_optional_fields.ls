type Rec = {name: string, opt?: int, tail: string}
type Nullable = {value: int?}
type OptionalValue = {opt?: int}
type RequiredValue = {opt: int}
type MissingValue = OptionalValue ! RequiredValue
type OpenRecord = {}
fn read_record(r: Rec) => [r.name, r.opt, r.tail]
fn read_optional(r: OptionalValue) => r.opt
let missing: Rec = {name: "a", tail: "z"}
let dynamic: Rec = parse("{\"name\":\"b\",\"tail\":\"y\"}", 'json')^
let present: Rec = {name: "c", opt: 7, tail: "x"};
[read_record(missing), read_record(dynamic), read_record(present),
 len(missing), 'opt' at missing, {name:"a",tail:"z"} is Rec,
 {name:"a",opt:null,tail:"z"} is Rec,
 {} is Nullable, {value:null} is Nullable,
 {} is MissingValue, {opt:7} is MissingValue,
 read_optional({}), read_optional({opt:7}),
 OpenRecord <: OptionalValue, OptionalValue <: RequiredValue,
 RequiredValue <: OptionalValue]
