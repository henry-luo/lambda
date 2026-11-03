// S7.7.3: a rejected tail-call argument remains the call's error value.
fn rebuild(value, index: int) => if (value is error or index >= 1) {data: value}
    else rebuild(error("tail argument rejected"), index + 1)

rebuild([], 0)
