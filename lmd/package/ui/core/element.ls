// static tag registries share invocation; style-family tags remain ordinary elements (D9.3.4).
pub fn create(tags,kind,attributes,children) any^ => tags[string(kind)](attributes,children)
