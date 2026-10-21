// S9.2.2: cached previews are immutable; region updates require the exact preceding scene.
import img: lambda.image
import guides: .guides

// guide metadata has no painted content; retain every other scene dependency in the key.
pub fn visual(image) any^ => if (image == null or guides.container(image) == null) image else
    img.with_children(image,[for (child in content(image) where child != guides.container(image)) child])^

// neighborhood effects and styles can change pixels beyond the edited source region.
fn local_composition(node) => all([for (child in content(node))
    if (name(child) in ['effect','style']) false
    else if (name(child) in ['stack','layer']) local_composition(child) else true])
pub fn frame(st) any^ {
    let repaint = st.gesture.repaint
    let patchable = repaint != null and st.painted_doc != null and st.painted_doc == repaint.before and
        st.painted_raster.width == st.doc.width and st.painted_raster.height == st.doc.height and local_composition(st.doc)
    let rect = if (not patchable or repaint.rect == null) null else
        [max(0,repaint.rect[0]),max(0,repaint.rect[1]),min(st.doc.width,repaint.rect[2]),min(st.doc.height,repaint.rect[3])]
    let changed = if (patchable) rect != null and rect[2]>rect[0] and rect[3]>rect[1]
        else st.painted_doc == null or (st.painted_doc != st.doc and visual(st.painted_doc)^ != visual(st.doc)^);
    {changed:changed,raster:if (not changed) st.painted_raster
        else if (patchable) img.with_region(st.painted_raster,rect,img.render_region(st.doc,rect)^.pixels)^
        else img.render_frame(st.doc)^}
}
