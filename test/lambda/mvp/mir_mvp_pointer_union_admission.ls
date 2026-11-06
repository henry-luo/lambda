// D2.3.5/S7.6.1v9: both recovery arms admit the same native pointer lane.
fn image(present) element^ => if (present) <image width: 1> else error("missing")
fn recovered(present) element => image(present) ^ { <texture id: "fallback"> };
[name(recovered(true)), name(recovered(false))]
