// D4.3.1/D2.3.5: inferred storage retains each producer's actual outcome.
pub fn marker(flag) any^ {
    let text = if (flag) "a" else "b";
    "prefix" ++ text
}
fn endpoints(values, count: int) float => values[0] + values[count - 1]
pub fn coordinate_pair(x: float, y: float) float^ => endpoints([x, y], 2)
fn inferred_buffers(count: int) any^ {
    let kept = fill(4, 3)
    let rejected = fill(count, 1);
    [len(kept)^, kept[0], rejected is error]
}
pub fn selected(value) any^ {
    let text = match value {
        case int: "integer"
        case string: "string"
        default: "other"
    };
    text
}
fn conditioned(source) any^ {
    let text = if (source.flag) "a" else "b";
    text
};
[marker(true)^, marker(false)^, coordinate_pair(1.0, 2.0)^,
 inferred_buffers(-1)^, selected(1)^, selected("x")^, selected(false)^,
 conditioned({flag: error("condition")}) is error]
