// Nested functional replacement mirrors the path parser used by Raphaël.
var source = 'M195.559A77.66,77.66,0,0,0,52.351L26.047,126.150Z';
var command_pattern = /([achlmrqstvz])\s*((-?\d*\.?\d*(?:e[-+]?\d+)?\s*,?\s*)+)/ig;
var value_pattern = /-?\d*\.?\d*(?:e[-+]?\d+)?/ig;

var parsed = source.replace(command_pattern, function (match, command, values) {
    var numbers = [];
    values.replace(value_pattern, function (value) {
        numbers.push(+value);
        return value;
    });
    return command + numbers.length;
});

console.log(parsed);
