// declarative actions carry data to an ancestor handler; projection never
// writes application state or calls a procedure (S12.1.3, D7.2.5).
pub fn validate(binding) bool^ {
    if (binding == null or (binding is string and len(binding) > 0)) true
    else if (not (binding is map)) raise error("control: on_change must be an action name or descriptor")
    else if (not (binding.action is string) or len(binding.action) == 0)
        raise error("control: on_change.action must be a nonempty string")
    else if (binding.param != null and (not (binding.param is string) or len(binding.param) == 0))
        raise error("control: on_change.param must be a nonempty string")
    else if (binding.params != null and not (binding.params is map))
        raise error("control: on_change.params must be a map")
    else if (any([for (key, value in binding) not contains(["action", "param", "params"], string(key))]) or false)
        raise error("control: unknown on_change descriptor field")
    else true
}

pub fn request(binding, id, value) map {
    let descriptor = if (binding is string) {action: binding} else binding
    let key = descriptor.param or "value"
    {id: id, action: descriptor.action, value: value,
        params: {*: descriptor.params or {}, [key]: value}}
}
