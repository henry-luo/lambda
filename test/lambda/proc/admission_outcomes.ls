// S7.7.2v2/S7.7.4v2: failed local validation stores an outcome and continues.
type CheckedRecord = {value: int}
type ChoiceRecord = {name: string, choice: int | string}
fn opaque(value) => value
fn opaque_any(value) any => value
fn counted_empty() int[1] => []
fn failed() int^ => error("propagated")
pn inferred_propagation() { failed()^; 7 }
pn inferred_unary_propagation() { let outcome = failed(); outcome^; 8 }
pn message_recovery() { failed() ^ { ^.message }; 42 }
pn inferred_return(value) {
    if (true) { let checked: int = value; return checked }
    return 7
}
pn main() {
    let bad: int = opaque("bad")
    let good: int = opaque(7)
    var changed: int = 1
    changed = opaque("new failure")
    let comma_bad: int = opaque("bad"), comma_good: int = opaque(8)
    let nested = { let value: int = opaque("bad"); value is error }
    let arr: int[] = opaque([1, "bad"])
    let float_source: float[] = [1.0]
    let failed_literal: float[] = [float_source[3]]
    let failed_record: CheckedRecord = {value: opaque("bad")}
    let empty_counted: int[1] = []
    let empty_returned = counted_empty()
    let small: u32 = opaque(-1)
    let inferred = error("captured")
    let broad: any = error("captured too");
    let checked_after_handler: int = opaque("bad") ^ { 99 }
    let after_binding = checked_after_handler ^ { 99 };
    let broad_source: int = opaque_any("bad");
    let returned = inferred_return(null);
    let propagated = inferred_propagation()
    let propagated_value = inferred_unary_propagation();
    let numeric_choice: ChoiceRecord = {name: "fixed", choice: 1}
    let string_choice: ChoiceRecord = {name: "checked", choice: opaque("ok")}
    let invalid_choice: ChoiceRecord = {name: "rejected", choice: opaque(true)};
    [bad is error, good, changed is error, comma_bad is error, comma_good,
     nested, arr is error, small is error, inferred.message, broad.message,
     checked_after_handler is error, after_binding, broad_source is error, returned is error,
     propagated.message, propagated_value.message, message_recovery(), empty_counted is error, empty_returned is error, failed_literal is error, failed_record is error,
     numeric_choice.choice, string_choice.choice, invalid_choice is error]
}
