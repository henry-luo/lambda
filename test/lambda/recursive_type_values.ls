// S11.4.12/S1.6: structural recursion remains a usable first-class type value.
type Chain = int | {next: Chain?}
type Left = {right: Right?}
type Right = {left: Left?}
fn identity(value) => value
let values = [Chain, Left, Right]
let saved = {chain: identity(values[0]), left: values[1]}
let chain = saved.chain
let left = saved.left
let right = values[2]
let result = [
    values[0] == Chain,
    3 is chain,
    {next: {next: 5}} is chain,
    {next: "bad"} is chain,
    {right: {left: null}} is left,
    {left: {right: null}} is right
]
result
