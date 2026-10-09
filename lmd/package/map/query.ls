import models: .model
import frames: .frame

fn value(v, fallback) => if (v == null) fallback else v
fn features(data) => if (data.type == "FeatureCollection") data.features
    else if (data.type == "Feature") [data]
    else [{type:"Feature",properties:{},geometry:data}]
fn sources(model, source_id) => [for (child in content(model) where name(child) == 'source' and string(child.id) == string(source_id)) child]
fn source_records(model, source_id) {
    let source = sources(model,source_id);
    if (len(source) != 1) error("map: query source does not exist")
    else [for (index, feature in features(source[0].data))
        {source:string(source_id),feature_id:if (feature.id != null) feature.id else index,feature:feature,geometry:feature.geometry}]
}
pub fn query_source(model, source_id) {
    let checked = models.normalize(model);
    if (checked is error) checked else source_records(checked,source_id)
}
pub fn query_rendered(model_or_frame, point_or_box, options = {}) {
    let frame=if (model_or_frame.type=="geomap-frame") model_or_frame else frames.plan(model_or_frame);
    if (frame is error) frame else frames.query(frame,point_or_box,options)
}
