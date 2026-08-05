// Nullable tagged scalars cross the direct ABI as raw pointers. Complex is a
// direct-pointer Item, while decimal and datetime restore their tag only when
// the result is collected into the generic output array.
fn pass_decimal(value: decimal?) decimal? => value
fn pass_datetime(value: datetime?) datetime? => value
fn pass_complex(value: complex?) complex? => value

pn main() {
    let missing_decimal: decimal? = pass_decimal(null)
    let present_decimal: decimal? = pass_decimal(1.25m)
    let missing_datetime: datetime? = pass_datetime(null)
    let present_datetime: datetime? = pass_datetime(t'2025-01-02')
    let missing_complex: complex? = pass_complex(null)
    let present_complex: complex? = pass_complex(2 + 3j)
    print([missing_decimal, present_decimal, missing_datetime, present_datetime,
        missing_complex, present_complex])
}
