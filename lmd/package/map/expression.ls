import radiant: radiant

// painting, export and queries share the native expression compiler (D9.5.6).
pub fn validate(layer) any^ {
    let result = radiant.geomap_validate_layer(layer);
    if (result is string) error("map: " ++ result) else result
}
pub fn evaluate(layer, feature, zoom) any^ {
    let result = radiant.geomap_eval_layer(layer,feature,zoom);
    if (result is string) error("map: " ++ result) else result
}
pub fn evaluate_features(layer, features, zoom) any^ {
    let result = radiant.geomap_eval_features(layer,features,zoom);
    if (result is string) error("map: " ++ result) else result
}
