import ._mir_mvp_expansion_dependency

type Box = {values: int[], extra: array}
type Counter = {value: int}
type CounterRows = {rows: Counter?[]}
type MixedCounter = {amount: float, flag: bool}
type MixedRows = {rows: MixedCounter[]}
type BranchLeaf = {leaf: int}
type Branch = BranchLeaf | {left: Branch, right: Branch}
type ShapeProbe = {mvp_guarded_count: int}

fn word_or_empty(words: string[], index: int) string =>
    if (index < len(words)) words[index] else ""

fn failed_text(message: string) string^ { raise error(message) }
fn failed_counter_value() Counter?^ { raise error("receiver") }
fn failed_mixed_value() MixedCounter?^ { raise error("flag") }

fn raised_number(n: int) int^ { raise error(string(n)) }

fn default_local(value: int, other: int = value + 2) int => value + other

fn allocated_box(n: int) Box^ => {values: [n, n + 1], extra: []}
fn failed_values() int[]^ { raise error("owner") }
fn outcome_index(values: int[]^, index: int^) int?^ => values[index]
fn projected_index(box: Box, index: int^) int?^ => box.values[index]
fn optional_rows(owner: CounterRows?) int => if (owner == null) 0 else len(owner.rows)
fn optional_counter(counter: Counter?) int => if (counter != null) counter.value else 0
fn optional_flag(flag: bool?) bool => if (flag == null) false else flag
fn required_flag(flag: bool?) bool^ { let admitted: bool = flag; admitted }
fn required_text(text: string?) string^ { let admitted: string = text; admitted }
fn required_text_outcome(text: string^) string^ { let admitted: string = text^; admitted }
fn plain_map(value: any) map^ { let admitted: map = value; admitted }
fn plain_array(value: any) array^ { let admitted: array = value; admitted }
fn dynamic_member(value: any) any => value.mvp_extra_payload
fn member_arms(value: any, choose: bool) any => if (choose) value.value else value.value
fn candidate_member(value: any) any => value.mvp_guarded_count
fn compare_flags(left: bool?, right: bool?) bool => left == right
fn compare_projected_flag(value: MixedCounter?^) bool^ => value.flag == true
fn compare_outcomes(left: any, right: any) any => if (left < right) 1 else 0
fn append_path(prefix: array, value: any) array^ => prefix ++ [value]
fn make_branch(depth: int) Branch^ {
    if (depth == 0) { var leaf = {leaf: 1}; return leaf }
    var branch = {left: make_branch(depth - 1), right: make_branch(depth - 1)}
    return branch
}
pn accumulate_paths() int^ {
    var values: array = [{value: 1}]
    let saved = values
    values = values ++ [{value: 2}]
    let prior = values
    values = values ++ values
    values = values ++ [values[0]]
    return len(saved) * 1000 + len(prior) * 100 + len(values) * 10 + values[3].value
}
pn literal_children() int^ {
    var child = {value: 1}
    var projected: array = [child]
    let retained: array = [projected[0]]
    child.value = 2
    projected[0].value = 3
    var fresh: array = [{value: 4}]
    fresh[0].value = 5
    return retained[0].value * 100 + child.value * 10 + fresh[0].value
}
pn container_field_captures() int^ {
    var left: Box = allocated_box(1)^
    var right: Box = allocated_box(2)^
    let saved = left
    left.values = right.values
    right.values[0] = 7
    left.values[0] = 9
    return saved.values[0] * 100 + right.values[0] * 10 + left.values[0]
}
pn append_integers(count: int) int^ {
    var values: int[] = []
    var index: int = 0
    while (index < count) { values.push(index)^; index = index + 1 }
    let saved = values
    values.push(count)^
    return len(saved) * 100 + len(values) * 10 + values[count]
}
fn optional_counter_outcome(counter: Counter?^) int^ => if (counter == null) 0 else counter.value
fn increment_observation(counter: Counter?^) int?^ {
    var observed = counter.value
    observed = observed + 1
    observed
}

// unchanged owners stay rooted across implicit tail edges, including a checked scalar actual.
fn implicit_collect(values: int[], count: int, total: int) int^ {
    if (count == 0) total
    else {
        let garbage = fill(8, count)
        implicit_collect(values, count - 1, total + values[0])
    }
}
fn implicit_record_rows(rows: Counter[], index: int, count: int, total: int) int^ =>
    if (index >= count) total
    else implicit_record_rows(rows, index + 1, count, total + rows[index].value)
fn split_observation(text: string, separator: string) int^ {
    let words = split(text, separator)
    len(words) * 10 + len(word_or_empty(words, 0))
}
fn float_values(input: any) float[]^ => input
fn outcome_float_read(input: any, index: int) float?^ {
    let values = float_values(input)
    values[index]
}
fn demand_float(value: float) float => value
fn required_float_read(input: any, index: int) float^ => demand_float(outcome_float_read(input, index))
fn erased_index(input: any, index: int) any => input[index]
fn box_input(input: any) Box^ => input
fn observe_box(box: Box) int => len(box.values)
pn updated_box(input: any) int^ {
    var box = box_input(input)
    box.values = [3, 4, 5]
    observe_box(box)
}

fn same_text(a: string?, b: string?) bool => a == b
fn literal_text(text: string?^) bool^ => text == "é\u0000"
fn selected_character(text: string, index: int) string?^ => text[index]
fn semantic_kind(value: any) int =>
    if (value is float) 1 else if (value is bool) 2 else if (value is string) 3
    else if (value is map) 4 else if (value is array) 5 else if (value is error) 6
    else if (value is null) 7 else 0
fn dynamic_null(value: any) bool^ => value == null
fn integer_text(value: int^) string^ => "i=" ++ value ++ ";"

pn assign_slot(var values: int[], index: int, value: int) any { values[index] = value }
pn replace_slots(var values: int[], value: int) any { values = [value] }
pn borrowed_values(count: int) int[]^ {
    var values = imported_numbers(count)^
    assign_slot(values, 0, 8)^
    return values
}
pn overwrite_coarse(var values: array) any { values = [1, "mixed"] }
pn coarse_borrow_result() any {
    var values: array = imported_numbers(2)^
    overwrite_coarse(values)
    return values[1]
}
pn update_counter(var counter: Counter, value: int) any {
    counter.value = counter.value + value
}
pn set_optional_counter(var counter: Counter, value: int?) any { counter.value = value }
pn fail_counter(var counter: Counter) any {
    let failed = raised_number(7)
    counter.value = failed
}
fn read_record_outcome(input: any) any {
    let counter: Counter = input
    counter.value
}
pn write_record_outcome(input: any) any {
    var counter: Counter = input
    counter.value = 3
    counter.value
}
pn write_grid(var grid: int[][], row: int, col: int, value: int) any {
    grid[row][col] = value
}
pn write_sequence(var grid: (int*)*, row: int, col: int, value: int) any {
    grid[row][col] = value
}
pn set_row(var owner: CounterRows, index: int, value: int) any {
    owner.rows[index].value = value
}
pn set_optional_row(var owner: CounterRows, index: int, value: int?) any {
    owner.rows[index].value = value
}
pn set_mixed_row(var owner: MixedRows, index: int, value: float) any {
    owner.rows[index].flag = true
    owner.rows[index].amount = value
}
pn observe_and_assign(prior: Box, var values: int[]) int {
    values[0] = 31
    prior.values[0]
}

fn dot_part(a: float[], b: float[], count: int) float^ {
    var total: float = 0.0
    var i: int = 0
    while (i < count) {
        total = total + a[i] * b[i]
        i = i + 1
    }
    total
}

pn main() {
    print("module: " ++ first_seed() ++ " " ++ default_external(3) ++ " " ++ dependency_rows[0][1] ++ "\n")
    print("defaults: " ++ default_local(3) ++ " " ++ default_local(3, 8) ++ "\n")
    let admitted_box: Box = allocated_box(1)^
    var box: Box = admitted_box
    let saved: Box = box
    replace_first(box.values, 7)^
    box.extra.push([9])^
    print("places: " ++ saved.values[0] ++ " " ++ box.values[0] ++ " " ++ len(saved.extra) ++ " " ++ len(box.extra) ++ "\n")

    var slots: Box = allocated_box(4)^
    let old_slots: Box = slots
    assign_slot(slots.values, 1, 12)^
    assign_slot(slots.values, 0, 10)^
    print("borrow: " ++ old_slots.values[0] ++ " " ++ old_slots.values[1] ++ " " ++ slots.values[0] ++ " " ++ slots.values[1] ++ "\n")
    replace_slots(slots.values, 21)^
    print("borrow-replace: " ++ len(slots.values) ++ " " ++ slots.values[0] ++ "\n")

    print("borrow-alias: " ++ observe_and_assign(slots, slots.values) ++ " " ++ slots.values[0] ++ "\n")
    assign_slot(slots.values, 0, slots.values[0] + 1)^
    print("borrow-scalar: " ++ slots.values[0] ++ "\n")

    var counter: Counter = {value: 3}
    let saved_counter: Counter = counter
    update_counter(counter, 4)^
    let failed_counter = fail_counter(counter)
    print("record-store: " ++ saved_counter.value ++ " " ++ counter.value ++ " " ++ (type(failed_counter) == error) ++ "\n")
    print("record-outcome: " ++ (type(read_record_outcome("bad")) == error) ++ " " ++ (type(write_record_outcome("bad")) == error) ++ " " ++ write_record_outcome({value: 1}) ++ "\n")
    var counter_rows: CounterRows = {rows: [{value: 1}, null]}
    let saved_rows = counter_rows^
    set_row(counter_rows, 0, 2)^
    let saved_row = counter_rows.rows[0]
    set_row(counter_rows, 0, 3)^
    let null_row = set_row(counter_rows, 1, 4)
    let absent_row = set_row(counter_rows, 4294967296, 5)
    print("record-spine: " ++ saved_rows.rows[0].value ++ " " ++ saved_row.value ++ " " ++ counter_rows.rows[0].value ++ " " ++ (type(null_row) == error) ++ " " ++ (type(absent_row) == error) ++ "\n")
    set_optional_row(counter_rows, 0, 8)^
    let absent_value = set_optional_row(counter_rows, 0, null)
    print("optional-spine: " ++ counter_rows.rows[0].value ++ " " ++ saved_row.value ++ " " ++ (type(absent_value) == error) ++ "\n")
    var mixed_rows: MixedRows = {rows: [{amount: 0.5, flag: false}]}
    let saved_mixed = mixed_rows^
    set_mixed_row(mixed_rows, 0, 1.25)^
    set_mixed_row(mixed_rows, 0, 2.5)^
    print("mixed-spine: " ++ saved_mixed.rows[0].amount ++ " " ++ saved_mixed.rows[0].flag ++ " " ++ mixed_rows.rows[0].amount ++ " " ++ mixed_rows.rows[0].flag ++ "\n")
    let indexed_box: Box = allocated_box(5)^
    let missing_index = projected_index(indexed_box, 10)
    let failed_index = raised_number(7)
    print("indexed-outcome: " ++ projected_index(indexed_box, 0) ++ " " ++ (missing_index == null) ++ " " ++ ((missing_index < 3) == null) ++ " " ++ ((missing_index + 3) == null) ++ " " ++ (type(projected_index(indexed_box, failed_index)) == error) ++ "\n")
    print("indexed-order: " ++ (outcome_index(failed_values(), failed_index) ^ { ^.message }) ++ "\n")
    print("null-branch: " ++ optional_rows(null) ++ " " ++ optional_rows(counter_rows) ++ " " ++ optional_counter(null) ++ " " ++ optional_counter(counter) ++ " " ++ optional_flag(null) ++ " " ++ optional_flag(true) ++ "\n")
    print("null-branch-error: " ++ (optional_counter_outcome(failed_counter_value()) ^ { ^.message }) ++ "\n")
    // present native lanes skip validation; absence and error payloads retain
    // the ordinary checked boundary (D2.3.5, S11.4.1v5).
    print("scalar-admission: " ++ required_flag(false) ++ " " ++ required_flag(true) ++ " " ++ (type(required_flag(null)) == error) ++ " " ++ required_text("ready") ++ " " ++ (type(required_text(null)) == error) ++ " " ++ (required_text_outcome(failed_text("scalar")) ^ { ^.message }) ++ "\n")
    print("container-admission: " ++ plain_map({value: 3}).value ++ " " ++ len(plain_array([1, 2])) ++ " " ++ len(plain_array([[1, 2], [3, 4]])) ++ " " ++ (type(plain_map(7)) == error) ++ " " ++ (type(plain_map(null)) == error) ++ " " ++ (type(plain_array(7)) == error) ++ " " ++ (type(plain_array(null)) == error) ++ "\n")
    print("member-key: " ++ dynamic_member({mvp_extra_payload: 7}) ++ " " ++ (dynamic_member({other: 8}) == null) ++ " " ++ (dynamic_member(failed_text("member")) ^ { ^.message }) ++ "\n")
    print("member-arms: " ++ member_arms({value: 11}, true) ++ " " ++ member_arms({value: 12}, false) ++ "\n")
    print("comparison-domains: " ++ compare_flags(null, null) ++ " " ++ compare_flags(null, false) ++ " " ++ compare_flags(false, false) ++ " " ++ compare_flags(true, false) ++ " " ++ compare_outcomes(null, 1) ++ " " ++ compare_outcomes(1, 2) ++ " " ++ (compare_outcomes(failed_text("compare"), 1) ^ { ^.message }) ++ "\n")
    print("comparison-projection: " ++ compare_projected_flag({amount: 1.0, flag: true}) ++ " " ++ compare_projected_flag(null) ++ " " ++ (compare_projected_flag(failed_mixed_value()) ^ { ^.message }) ++ "\n")
    var appended_counter: Counter = {value: 5}
    let appended_path: array = append_path([], appended_counter)^
    update_counter(appended_counter, 1)^
    print("path-append: " ++ appended_path[0].value ++ " " ++ appended_counter.value ++ " " ++ len(append_path([1, 2], 3)) ++ " " ++ append_path([], "x")[0] ++ "\n")
    print("accumulate: " ++ accumulate_paths() ++ " " ++ make_branch(3).left.right.left.leaf ++ " " ++ (type(make_branch(0)) == BranchLeaf) ++ "\n")
    let shape_probe: ShapeProbe = {mvp_guarded_count: 9}
    print("candidate-member: " ++ candidate_member(shape_probe) ++ " " ++ candidate_member({mvp_guarded_count: "other"}) ++ " " ++ (candidate_member({different: 1}) == null) ++ " " ++ (candidate_member(failed_text("candidate")) ^ { ^.message }) ++ "\n")
    var optional_counter_slot: Counter = {value: 1}
    let saved_counter_slot = optional_counter_slot
    set_optional_counter(optional_counter_slot, 12)^
    let refused_counter_slot = set_optional_counter(optional_counter_slot, null)
    print("optional-field: " ++ optional_counter_slot.value ++ " " ++ saved_counter_slot.value ++ " " ++ (type(refused_counter_slot) == error) ++ "\n")
    print("append-growth: " ++ append_integers(40) ++ " " ++ append_integers(0) ++ "\n")
    let imported_values = imported_numbers(3)
    let imported_failed = imported_numbers(-1)
    let tiny_import = imported_tiny()
    let other_import = imported_tiny()
    print("imported-domain: " ++ len(imported_values) ++ " " ++ imported_values[2] ++ " " ++ (imported_failed ^ { ^.message }) ++ " " ++ (tiny_import == other_import) ++ "\n")
    print("borrow-domain: " ++ borrowed_values(3)[0] ++ " " ++ (borrowed_values(-1) ^ { ^.message }) ++ " " ++ coarse_borrow_result() ++ "\n")
    print("projection-outcome: " ++ increment_observation({value: 3}) ++ " " ++ (increment_observation(null) == null) ++ " " ++ (increment_observation(failed_counter_value()) ^ { ^.message }) ++ "\n")
    print("implicit-tail: " ++ implicit_collect([3], 64, 0) ++ " " ++ (type(implicit_collect([], 1, 0)) == error) ++ " " ++ implicit_record_rows([{value: 2}, {value: 5}], 0, 2, 0) ++ " " ++ (type(implicit_record_rows([], 0, 1, 0)) == error) ++ "\n")
    let failed_words = split("a b", failed_text("separator"))
    print("split-observation: " ++ split_observation("a b", " ") ++ " " ++ split_observation("é猫", "") ++ " " ++ split_observation("", "") ++ " " ++ (len(failed_words) ^ { ^.message }) ++ "\n")
    print("float-cache: " ++ outcome_float_read([1.25, 2.5], 1) ++ " " ++ (outcome_float_read([1.25], 4294967296) == null) ++ " " ++ (type(outcome_float_read("bad", 0)) == error) ++ "\n")
    print("float-admission: " ++ required_float_read([1.25], 0) ++ " " ++ (type(required_float_read([], 0)) == error) ++ " " ++ (type(required_float_read("bad", 0)) == error) ++ "\n")
    let nullable_floats: float?[] = [null, 2.5]
    print("erased-float: " ++ erased_index([1.25, 2.5], 1) ++ " " ++ (erased_index(nullable_floats, 0) == null) ++ " " ++ (erased_index([1.25], -1) == null) ++ " " ++ (erased_index([1.25], 4294967296) == null) ++ "\n")
    let boxed_words: string[] = ["left", "right"]
    print("boxed-array-read: " ++ erased_index([1, "text", {value: 3}], 1) ++ " " ++ erased_index(boxed_words, 1) ++ " " ++ (erased_index([1, "text"], -1) == null) ++ " " ++ (erased_index([1, "text"], 4294967296) == null) ++ " " ++ (erased_index(failed_values(), 0) ^ { ^.message }) ++ "\n")
    print("literal-children: " ++ literal_children() ++ "\n")
    print("container-field: " ++ container_field_captures() ++ "\n")
    print("integer-text: " ++ integer_text(4294967296) ++ " " ++ (integer_text(raised_number(7)) ^ { ^.message }) ++ "\n")
    // a record outcome's field write may change its layout; the typed call
    // re-admits the updated owner without altering the retained input (D2.4.3, S9.1.2).
    let original_box: Box = {values: [1, 2], extra: []}
    print("record-layout: " ++ updated_box(original_box) ++ " " ++ len(original_box.values) ++ "\n")

    var grid: int[][] = [[1, 2], [3, 4]]
    let saved_grid: int[][] = grid^
    write_grid(grid, 1, 0, 9)^
    write_grid(grid, 0, 1, 11)^
    let failed_grid = write_grid(grid, 4294967296, 0, 12)
    let negative_grid = write_grid(grid, 0, -1, 13)
    print("packed-store: " ++ saved_grid[1][0] ++ " " ++ grid[1][0] ++ " " ++ grid[0][1] ++ " " ++ (type(failed_grid) == error) ++ " " ++ (type(negative_grid) == error) ++ "\n")
    var sequence: (int*)* = [for (row in 0 to 1) fill(2, 0)]
    let saved_sequence = sequence^
    write_sequence(sequence, 0, 0, 5)^
    write_sequence(sequence, 1, 1, 6)^
    print("sequence-store: " ++ saved_sequence[0][0] ++ " " ++ sequence[0][0] ++ " " ++ sequence[1][1] ++ "\n")

    print("kernel: " ++ (dot_part([1.0, 2.0], [3.0, 4.0], 2) == 11.0) ++ " " ++ (dot_part([], [], 0) == 0.0) ++ " " ++ (type(dot_part([1.0], [2.0], 2)) == error) ++ "\n")

    var boxes: Box?[] = [allocated_box(1)^, null]
    let kept_box = boxes[0]
    boxes[0].values[0] = 9
    print("pointer-read: " ++ (boxes[1] == null) ++ " " ++ (boxes[5] == null) ++ " " ++ kept_box.values[0] ++ " " ++ boxes[0].values[0] ++ "\n")

    var dynamic: array = []
    dynamic.push(0)^
    dynamic[0] = 9
    print("open-array: " ++ dynamic[0] ++ "\n")
    print("keys: " ++ join([for (key, item at {a: 2, b: 3}) string(key) ++ string(item)], ",") ++ "\n")
    let text: string = "λx"
    print("text: " ++ join([for (index of text) chr(ord(text[index]))], "") ++ "\n")
    print("text-compare: " ++ same_text(null, null) ++ " " ++ same_text(null, "") ++ " " ++ same_text(text, "λx") ++ "\n")
    print("character: " ++ (text[0] == "λ") ++ " " ++ ("x" == text[1]) ++ " " ++ (text[4] != "x") ++ " " ++ ("\u0000"[0] == "\u0000") ++ "\n")
    print("literal-text: " ++ literal_text("é\u0000") ++ " " ++ literal_text(null) ++ " " ++ literal_text("") ++ " " ++ (literal_text(failed_text("literal")) ^ { ^.message }) ++ "\n")
    print("selected-character: " ++ selected_character("ab", 1) ++ " " ++ selected_character("é猫", 1) ++ " " ++ (selected_character("ab", 4294967296) == null) ++ " " ++ (selected_character("ab", -1) == null) ++ "\n")
    print("semantic-kind: " ++ semantic_kind(1e-300) ++ " " ++ semantic_kind(false) ++ " " ++ semantic_kind("x") ++ " " ++ semantic_kind({v: 1}) ++ " " ++ semantic_kind([1, 2]) ++ " " ++ semantic_kind(failed_text("kind")) ++ " " ++ semantic_kind(null) ++ "\n")
    print("dynamic-null: " ++ dynamic_null(null) ++ " " ++ dynamic_null(0) ++ " " ++ dynamic_null([]) ++ " " ++ dynamic_null(type(null)) ++ " " ++ (dynamic_null(failed_text("absence")) ^ { ^.message }) ++ "\n")
    let empty = [for (item in [1, 2] where item > 3) item]
    print("empty: " ++ len(empty) ++ "\n")
    let ranked = sort([1, 3, 2], (item) => 0 - item)
    print("sort: " ++ ranked[0] ++ " " ++ ranked[1] ++ " " ++ ranked[2] ++ "\n")
    let groups = [for (item in [2, 1, 2] group by item as key into members order by key)
        {key: key, count: len(members)}]
    print("groups: " ++ groups[0].key ++ ":" ++ groups[0].count ++ " " ++ groups[1].key ++ ":" ++ groups[1].count ++ "\n")
    let computed = {[string(3)]: 4, plain: 5}
    print("map: " ++ computed["3"] ++ " " ++ computed.plain ++ "\n")
    var dictionary: map = {}
    dictionary[string(3)] = 4
    let previous = dictionary
    dictionary["3"] = 6
    print("map-write: " ++ len(dictionary) ++ " " ++ previous["3"] ++ " " ++ dictionary["3"] ++ "\n")
    print("branch: " ++ word_or_empty(["kept"], 0) ++ ":" ++ word_or_empty(["kept"], 2) ++ "\n")
    print("recovery: " ++ join([for (n in [2, 3]) string(raised_number(n) ^ { int(^.message) ^ { 0 } })], ",") ++ "\n")
    print("membership: " ++ ("key" at {key: 1}) ++ " " ++ (1 in [0, 1]) ++ " " ++ (0 of [4]) ++ "\n")
    print("text-outcome: " ++ ((failed_text("first") == failed_text("second")) ^ { ^.message }) ++ "\n")
    print("observer: " ++ (type(error("observed")) == error) ++ " " ++ contains("lambda", "mb") ++ "\n")
}
