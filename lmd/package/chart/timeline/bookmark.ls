// Encoded chart/event names keep addresses independent of source row positions.
import c: .common
import url: lambda.dom.urlencode
import theme: .theme

fn prefix(spec) => "#timeline=" ++ url.form_encode(c.text(spec.id)) ++ "/"
pub fn address(spec, id) {
    if (c.id(spec.id) is error) c.fail("story.bookmarks","an authored chart ID is required")
    else if (id==null or not contains([spec.intro.id,*[for (item in spec.events) item.id]],c.text(id))) c.fail("bookmark.id","unknown event")
    else prefix(spec) ++ url.form_encode(c.text(id))
}
pub fn resolve(spec, fragment) {
    let source=c.text(fragment);
    let head=prefix(spec);
    if (not starts_with(source,head)) null else {
        let found=[for (id in [spec.intro.id,*[for (item in spec.events) item.id]] where id!=null and address(spec,id)==source) id];
        if (len(found)==0) c.fail("bookmark","the addressed event is no longer available") else found[0]
    }
}
pub fn render(spec, item) => if (spec.story.bookmarks!=true or item.loading==true) null else
    <a *:theme.part(spec,"bookmark",{class:"lambda-timeline-bookmark",href:address(spec,item.id),
        'aria-label':c.message(spec,"bookmark","Link to event")}),c.message(spec,"bookmark","Link to event")>
