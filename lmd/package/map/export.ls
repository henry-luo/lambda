import frames: .frame

pub fn to_svg(model, viewport = null) {
    let frame=frames.plan(model,viewport);
    if (frame is error) frame else frames.render_frame(frame)
}
