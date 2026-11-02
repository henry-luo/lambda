// S7.5.4/S12.3.3v2: an error alternative preserves member-call resolution.
pub fn lower(source, label_format = "text") any^ => "shadowed"
fn safe_enum(value, allowed) any^ {
    let text = string(if (value != null) value else "").lower();
    if (contains(allowed, text)) text else null
}
fn text_outcome(failed: bool) string^ =>
    if (failed) error("text") else "LEFT"
type Custom {
    text: string,
    fn lower() string => text,
    pn visit() int {
        print("visited"); print("\n")
        return 7
    }
}
fn custom_outcome(failed: bool) Custom^ =>
    if (failed) error("object") else <Custom text: "user">
type Holder = {lower: fn() string}
fn field_outcome(failed: bool) Holder^ =>
    if (failed) error("field") else {lower: () => "field value"}
pn main() {
    let good = text_outcome(false).lower()
    let failed = text_outcome(true).lower()
    let chained = text_outcome(false).lower().upper()
    let failed_chain = text_outcome(true).lower().upper()
    let custom = custom_outcome(false)
    let bad_custom = custom_outcome(true)
    let method = custom.lower()
    let bad_method = bad_custom.lower()
    let holder = field_outcome(false)
    let bad_holder = field_outcome(true)
    let field = holder.lower()
    let bad_field = bad_holder.lower()
    var mutable_bad = custom_outcome(true)
    var mutable_good = custom_outcome(false)
    let refused = mutable_bad.visit()
    let visited = mutable_good.visit();
    [safe_enum("LEFT", ["left", "right"]), safe_enum("invalid", ["left"]),
     good, failed is error, failed.message, chained, failed_chain.message,
     method, bad_method is error, bad_method.message,
     field, bad_field is error, bad_field.message,
     refused is error, refused.message, visited]
}
