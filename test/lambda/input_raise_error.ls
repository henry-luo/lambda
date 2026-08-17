// input() loader failures must use the raised error channel, not null success.

let one = input("test/input/definitely_missing_for_input_raise.json", 'json') ^ { ^ }
{value: type(one), error: type(one)}

let two = input("test/input/definitely_missing_for_input_raise.json", {type: 'json'}) ^ { ^ }
{value: type(two), error: type(two)}
