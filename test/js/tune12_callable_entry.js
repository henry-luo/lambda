// T12-7: a method Get chooses one callable before argument evaluation; later
// property changes cannot change that selected callee or repeat the Get.
function first_method(value) { return "first:" + value; }
function second_method(value) { return "second:" + value; }

var getter_calls = 0;
var alternating = {
    get method() {
        getter_calls = getter_calls + 1;
        return getter_calls === 1 ? first_method : second_method;
    }
};
var alternating_first = alternating.method("a");
var alternating_second = alternating.method("b");

var getter_error = "none";
var throwing = {
    get method() { throw new Error("getter"); }
};
try {
    throwing.method(1);
} catch (error) {
    getter_error = error.message;
}

function old_method(value) { return "old:" + value; }
function new_method(value) { return "new:" + value; }
var changed_by_argument = { method: old_method };
function replace_method_argument() {
    changed_by_argument.method = new_method;
    return 7;
}
var selected_before_argument = changed_by_argument.method(replace_method_argument());
var selected_after_argument = changed_by_argument.method(8);

var receiver = { method: old_method };
receiver.method = new_method;
var replaced_receiver = receiver.method(9);

function overridden_join(separator) { return "override:" + separator + ":" + this.length; }
var values = [1, 2];
values.join = overridden_join;
var overridden_builtin = values.join("/");

function read_prefix(value) { return this.prefix + value; }
var bound = read_prefix.bind({ prefix: "bound:" });

console.log("alternate:" + alternating_first + "," + alternating_second + ":" + getter_calls);
console.log("getter:" + getter_error);
console.log("selected:" + selected_before_argument + "," + selected_after_argument);
console.log("receiver:" + replaced_receiver);
console.log("builtin:" + overridden_builtin);
console.log("bound:" + bound("ok"));
