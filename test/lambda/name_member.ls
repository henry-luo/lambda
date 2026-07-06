// Test name() system function and .name property
type Person { name: string, age: int }
let person = <Person name: "Ada", age: 37>
fn name_member_target(x) => x

'name on element:'; name(<div>)
'.name property:'; <div class:"test">.name
'name on symbol:'; name('my_symbol')
'name on type:'; name(type(42))
'user-defined name takes precedence:'; <div name:"custom">.name
'name accessor ignores user-defined name:'; name(<div name:"custom">)
'name on object:'; name(person)
'name on object type:'; name(type(person))
'name on function:'; name(name_member_target)

// Test name() on null returns null (silently skipped in output)
'name on null returns null:'; if (name(null) == null) "yes" else "no"
'name on anonymous function returns null:'; if (name((x: int) => x) == null) "yes" else "no"

// Test error .code and .message
fn make_error() int^ {
    raise error("this is an error message")
}
fn get_error_code() {
    let a^err = make_error()
    err.code
}
fn get_error_message() {
    let a^err = make_error()
    err.message
}
'error code:'; get_error_code()
'error message:'; get_error_message()
