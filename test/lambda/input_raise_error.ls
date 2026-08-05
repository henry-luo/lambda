// input() loader failures must use the raised error channel, not null success.

let one^err1 = input("test/input/definitely_missing_for_input_raise.json", 'json')
{value: type(one), error: type(err1)}

let two^err2 = input("test/input/definitely_missing_for_input_raise.json", {type: 'json'})
{value: type(two), error: type(err2)}
