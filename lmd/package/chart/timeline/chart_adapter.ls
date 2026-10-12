// the ordinary chart grammar delegates history marks to the canonical presenter.
import c: .common
import schema: .schema
import layout: .layout
import vector: .svg

pub fn supports(spec) => c.text(if (spec.mark.kind!=null) spec.mark.kind else spec.mark.type)=="timeline"

pub fn specification(spec) {
    let options={*:c.attrs(spec.timeline_options),*:c.attrs(spec.mark.timeline_options)};
    let encoding=c.attrs(spec.encoding);
    let channels=map([for (key in ["id","at","from","to","title","subtitle","content","display_date","group","subgroup","tags",
        "color","marker","size","opacity","tooltip","media","link","priority","metadata"] where encoding[key]!=null) (key,encoding[key])]);
    let clock=if (channels.at!=null or channels.from!=null) {} else if (encoding.x2!=null) {from:encoding.x,to:encoding.x2}
        else if (encoding.x!=null) {at:encoding.x} else {};
    {id:if (spec.id!=null) spec.id else "history",view:"axis",spacing:"time",width:spec.width,height:spec.height,
        padding:spec.padding,title:spec.title,*:options,data:if (spec.data!=null) spec.data else [],
        encoding:{*:channels,*:clock}}
}

pub fn render(spec, viewport = null, snapshot = null) {
    let canonical=schema.validate(specification(spec));
    let plan=if (canonical is error) canonical else layout.normalized(canonical,viewport,snapshot);
    if (plan is error) plan else vector.render(plan,snapshot,c.attrs(spec.timeline_options.export))
}
