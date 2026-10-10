// static tag registries share invocation; style-family tags remain ordinary elements (D7.2.4).
pub fn create(tags,kind,attributes,children) => tags[string(kind)](attributes,children)
